#include "student_store.h"

#include "pf_api.h"
#include "slotted_page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_RECORDS 250
#define TEST_RECORD_LENGTH 100

int main(void)
{
    char path[256];
    char records[TEST_RECORDS][TEST_RECORD_LENGTH];
    int rids[TEST_RECORDS];
    SPStore store;
    int i;
    int status;

    (void)snprintf(path, sizeof(path), "/tmp/toydb_sp_pf_test_%ld.dat", (long)getpid());

    PF_Init();

    status = SPStore_CreateFile(path);
    if (status != SPSTORE_OK) {
        fprintf(stderr, "Create failed: %s\n", SPStore_ErrorString(status));
        return EXIT_FAILURE;
    }

    status = SPStore_Open(&store, path);
    if (status != SPSTORE_OK) {
        fprintf(stderr, "Open failed: %s (PF=%d)\n",
                SPStore_ErrorString(status), store.last_pf_error);
        (void)SPStore_DestroyFile(path);
        return EXIT_FAILURE;
    }

    for (i = 0; i < TEST_RECORDS; ++i) {
        (void)snprintf(records[i], sizeof(records[i]), "student-%04d-", i);
        memset(records[i] + strlen(records[i]), 'A' + (i % 26),
               TEST_RECORD_LENGTH - strlen(records[i]));
        status = SPStore_Insert(&store, records[i], TEST_RECORD_LENGTH, &rids[i]);
        if (status != SPSTORE_OK) {
            fprintf(stderr, "Insert %d failed: %s (PF=%d)\n", i,
                    SPStore_ErrorString(status), store.last_pf_error);
            (void)SPStore_Close(&store);
            (void)SPStore_DestroyFile(path);
            return EXIT_FAILURE;
        }
    }

    /* Verify through PF while the file is still open. */
    for (i = 0; i < TEST_RECORDS; ++i) {
        char output[TEST_RECORD_LENGTH];
        uint16_t output_length = 0;

        status = SPStore_Get(&store, rids[i], output, sizeof(output), &output_length);
        if (status != SP_OK || output_length != TEST_RECORD_LENGTH ||
            memcmp(output, records[i], TEST_RECORD_LENGTH) != 0) {
            fprintf(stderr, "Read-back failed for record %d (status=%d)\n", i, status);
            (void)SPStore_Close(&store);
            (void)SPStore_DestroyFile(path);
            return EXIT_FAILURE;
        }
    }

    if (SPStore_Close(&store) != SPSTORE_OK) {
        fprintf(stderr, "Close before reopen failed\n");
        (void)SPStore_DestroyFile(path);
        return EXIT_FAILURE;
    }

    /* Reopen: this verifies durable page writes and current-page recovery. */
    status = SPStore_Open(&store, path);
    if (status != SPSTORE_OK) {
        fprintf(stderr, "Reopen failed: %s (PF=%d)\n",
                SPStore_ErrorString(status), store.last_pf_error);
        (void)SPStore_DestroyFile(path);
        return EXIT_FAILURE;
    }

    for (i = 0; i < TEST_RECORDS; ++i) {
        char output[TEST_RECORD_LENGTH];
        uint16_t output_length = 0;

        status = SPStore_Get(&store, rids[i], output, sizeof(output), &output_length);
        if (status != SP_OK || output_length != TEST_RECORD_LENGTH ||
            memcmp(output, records[i], TEST_RECORD_LENGTH) != 0) {
            fprintf(stderr, "Persistent read-back failed for record %d (status=%d)\n",
                    i, status);
            (void)SPStore_Close(&store);
            (void)SPStore_DestroyFile(path);
            return EXIT_FAILURE;
        }
    }

    /* Append after reopening; the implementation should resume in the last page. */
    {
        char extra[TEST_RECORD_LENGTH];
        char output[TEST_RECORD_LENGTH];
        uint16_t output_length = 0;
        int extra_rid;

        memset(extra, 'Z', sizeof(extra));
        status = SPStore_Insert(&store, extra, sizeof(extra), &extra_rid);
        if (status != SPSTORE_OK) {
            fprintf(stderr, "Append after reopen failed: %s\n",
                    SPStore_ErrorString(status));
            (void)SPStore_Close(&store);
            (void)SPStore_DestroyFile(path);
            return EXIT_FAILURE;
        }

        status = SPStore_Get(&store, extra_rid, output, sizeof(output), &output_length);
        if (status != SP_OK || output_length != sizeof(extra) ||
            memcmp(output, extra, sizeof(extra)) != 0) {
            fprintf(stderr, "Read-back of appended record failed\n");
            (void)SPStore_Close(&store);
            (void)SPStore_DestroyFile(path);
            return EXIT_FAILURE;
        }
    }

    if (SPStore_Close(&store) != SPSTORE_OK) {
        fprintf(stderr, "Final close failed\n");
        (void)SPStore_DestroyFile(path);
        return EXIT_FAILURE;
    }

    if (SPStore_DestroyFile(path) != SPSTORE_OK) {
        fprintf(stderr, "Destroy test file failed\n");
        return EXIT_FAILURE;
    }

    printf("PASS: PF-backed slotted-page file creation and page allocation\n");
    printf("PASS: insert and RID-based retrieval across multiple PF pages\n");
    printf("PASS: close/reopen persistence and append-page recovery\n");
    printf("PF integration tests verified %d records plus one reopened append\n",
           TEST_RECORDS);
    return EXIT_SUCCESS;
}
