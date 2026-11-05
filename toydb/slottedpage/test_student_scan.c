/*
 * Phase 4 functional test: sequential scanning across PF pages.
 * Covers empty-file scans, multi-page traversal, deleted-slot skipping,
 * compaction persistence, callback early-stop, and RID/record correctness.
 */
#include "student_store.h"
#include "pf_api.h"
#include "rid.h"
#include "slotted_page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_RECORDS 250
#define TEST_MAX_LENGTH 160
#define DELETED_COUNT 4

typedef struct SavedRecord {
    unsigned char bytes[TEST_MAX_LENGTH];
    uint16_t length;
    int rid;
    int deleted;
    int seen;
} SavedRecord;

typedef struct ScanContext {
    SavedRecord *records;
    int count;
    int failed;
} ScanContext;

typedef struct StopContext {
    int calls;
    int stop_after;
} StopContext;

static int fail(SPStore *store, const char *path, const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    if (store != NULL && store->fd >= 0)
        (void)SPStore_Close(store);
    if (path != NULL)
        (void)SPStore_DestroyFile(path);
    return EXIT_FAILURE;
}

static int verify_record_callback(int rid, const void *record,
                                  uint16_t record_length, void *context)
{
    ScanContext *scan = (ScanContext *)context;
    int i;

    for (i = 0; i < scan->count; ++i) {
        SavedRecord *expected = &scan->records[i];
        if (expected->rid != rid)
            continue;

        if (expected->deleted || expected->seen ||
            expected->length != record_length ||
            memcmp(expected->bytes, record, record_length) != 0) {
            scan->failed = 1;
            return -1;
        }

        expected->seen = 1;
        return SPSTORE_SCAN_CONTINUE;
    }

    scan->failed = 1; /* Scanner returned an unknown RID. */
    return -1;
}

static int stop_after_callback(int rid, const void *record,
                               uint16_t record_length, void *context)
{
    StopContext *stop = (StopContext *)context;
    (void)rid;
    (void)record;
    (void)record_length;

    ++stop->calls;
    return stop->calls >= stop->stop_after
               ? SPSTORE_SCAN_STOP
               : SPSTORE_SCAN_CONTINUE;
}

int main(void)
{
    char path[256];
    SavedRecord records[TEST_RECORDS];
    const int deleted_indices[DELETED_COUNT] = {4, 47, 105, 249};
    SPStore store;
    ScanContext scan;
    StopContext stop;
    uint64_t scanned = 0;
    int status;
    int i;
    int expected_live = TEST_RECORDS - DELETED_COUNT;

    memset(&store, 0, sizeof(store));
    store.fd = -1;
    memset(records, 0, sizeof(records));
    (void)snprintf(path, sizeof(path), "/tmp/toydb_sp_scan_%ld.pf", (long)getpid());

    PF_Init();

    status = SPStore_CreateFile(path);
    if (status != SPSTORE_OK)
        return fail(NULL, path, "create scan test file");

    status = SPStore_Open(&store, path);
    if (status != SPSTORE_OK)
        return fail(&store, path, "open scan test file");

    /* An empty PF file must scan successfully and return zero records. */
    status = SPStore_Scan(&store, verify_record_callback, NULL, &scanned);
    if (status != SPSTORE_OK || scanned != 0)
        return fail(&store, path, "empty-file scan should return zero records");

    /* Insert variable-length records; 250 records force multiple PF pages. */
    for (i = 0; i < TEST_RECORDS; ++i) {
        char prefix[40];
        size_t prefix_len;
        uint16_t length;

        (void)snprintf(prefix, sizeof(prefix), "SCAN-RECORD-%04d-", i);
        prefix_len = strlen(prefix);
        length = (uint16_t)(80 + (i % 61));
        memcpy(records[i].bytes, prefix, prefix_len);
        memset(records[i].bytes + prefix_len, 'A' + (i % 26), length - prefix_len);
        records[i].length = length;

        status = SPStore_Insert(&store, records[i].bytes,
                                records[i].length, &records[i].rid);
        if (status != SPSTORE_OK)
            return fail(&store, path, "insert variable-length scan test record");
    }

    /* Close/reopen before scanning to ensure the scan reads persistent pages. */
    if (SPStore_Close(&store) != SPSTORE_OK)
        return fail(NULL, path, "close before first scan");
    if (SPStore_Open(&store, path) != SPSTORE_OK)
        return fail(&store, path, "reopen before first scan");

    /* Delete records from different positions/pages. */
    for (i = 0; i < DELETED_COUNT; ++i) {
        int index = deleted_indices[i];
        int page_num;
        uint16_t slot_num;

        if (SPStore_Delete(&store, records[index].rid) != SP_OK)
            return fail(&store, path, "delete record before scan");
        records[index].deleted = 1;

        if (SP_RID_Decode(records[index].rid, &page_num, &slot_num) != 0)
            return fail(&store, path, "decode deleted record RID");
        (void)slot_num;

        /* Explicit compaction must not change any surviving RID. */
        if (SPStore_CompactPage(&store, page_num) != SP_OK)
            return fail(&store, path, "compact page containing deleted record");
    }

    if (SPStore_Close(&store) != SPSTORE_OK)
        return fail(NULL, path, "close after delete/compact");
    if (SPStore_Open(&store, path) != SPSTORE_OK)
        return fail(&store, path, "reopen after delete/compact");

    scan.records = records;
    scan.count = TEST_RECORDS;
    scan.failed = 0;
    scanned = 0;

    status = SPStore_Scan(&store, verify_record_callback, &scan, &scanned);
    if (status != SPSTORE_OK || scan.failed || scanned != (uint64_t)expected_live)
        return fail(&store, path, "full scan did not return the expected live records");

    for (i = 0; i < TEST_RECORDS; ++i) {
        if ((!records[i].deleted && records[i].seen != 1) ||
            (records[i].deleted && records[i].seen != 0))
            return fail(&store, path, "scan missed a live record or returned a deleted record");
    }

    /* Test callback-controlled early termination. */
    stop.calls = 0;
    stop.stop_after = 11;
    scanned = 0;
    status = SPStore_Scan(&store, stop_after_callback, &stop, &scanned);
    if (status != SPSTORE_OK || stop.calls != 11 || scanned != 11)
        return fail(&store, path, "callback early-stop behavior failed");

    if (SPStore_Close(&store) != SPSTORE_OK)
        return fail(NULL, path, "final close");
    if (SPStore_DestroyFile(path) != SPSTORE_OK)
        return fail(NULL, path, "destroy scan test file");

    printf("PASS: empty PF file sequential scan\n");
    printf("PASS: multi-page sequential scan and RID/record verification\n");
    printf("PASS: deleted slots skipped after deletion and compaction\n");
    printf("PASS: scan survives close/reopen\n");
    printf("PASS: callback-controlled early termination\n");
    printf("Scanned live records: %d of %d inserted (%d deleted)\n",
           expected_live, TEST_RECORDS, DELETED_COUNT);
    printf("Phase 4 synthetic sequential-scan test passed\n");
    return EXIT_SUCCESS;
}
