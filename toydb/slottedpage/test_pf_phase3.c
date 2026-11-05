/*
 * PF-backed integration test for persistent deletion and compaction.
 * Usage: ./test_pf_phase3
 */
#include "student_store.h"
#include "pf_api.h"
#include "rid.h"
#include "slotted_page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_LEN 1000U

static int fail(SPStore *store, const char *message, const char *path)
{
    fprintf(stderr, "FAIL: %s\n", message);
    if (store != NULL && store->fd >= 0)
        (void)SPStore_Close(store);
    if (path != NULL)
        (void)SPStore_DestroyFile(path);
    return EXIT_FAILURE;
}

static int check_record(SPStore *store, int rid,
                       const unsigned char *expected, uint16_t expected_len)
{
    unsigned char output[SP_PAGE_SIZE];
    uint16_t actual_len = 0;
    int status;

    status = SPStore_Get(store, rid, output, sizeof(output), &actual_len);
    return status == SP_OK && actual_len == expected_len &&
           memcmp(output, expected, expected_len) == 0;
}

int main(void)
{
    char path[256];
    unsigned char a[TEST_LEN], b[TEST_LEN], c[TEST_LEN], d[1500];
    SPStore store;
    int rid_a, rid_b, rid_c, rid_d;
    int page_b;
    uint16_t slot_b;
    int status;

    memset(&store, 0, sizeof(store));
    store.fd = -1;
    memset(a, 'A', sizeof(a));
    memset(b, 'B', sizeof(b));
    memset(c, 'C', sizeof(c));
    memset(d, 'D', sizeof(d));
    (void)snprintf(path, sizeof(path), "/tmp/toydb_sp_phase3_%ld.dat", (long)getpid());

    PF_Init();

    status = SPStore_CreateFile(path);
    if (status != SPSTORE_OK)
        return fail(NULL, "create PF file", path);

    status = SPStore_Open(&store, path);
    if (status != SPSTORE_OK)
        return fail(&store, "open PF file", path);

    if (SPStore_Insert(&store, a, sizeof(a), &rid_a) != SPSTORE_OK ||
        SPStore_Insert(&store, b, sizeof(b), &rid_b) != SPSTORE_OK ||
        SPStore_Insert(&store, c, sizeof(c), &rid_c) != SPSTORE_OK)
        return fail(&store, "insert three records into one PF page", path);

    if (SP_RID_Decode(rid_b, &page_b, &slot_b) != 0)
        return fail(&store, "decode record B RID", path);

    if (!check_record(&store, rid_a, a, sizeof(a)) ||
        !check_record(&store, rid_b, b, sizeof(b)) ||
        !check_record(&store, rid_c, c, sizeof(c)))
        return fail(&store, "initial RID-based reads", path);

    if (SPStore_Delete(&store, rid_b) != SP_OK)
        return fail(&store, "PF-backed record deletion", path);

    {
        unsigned char output[SP_PAGE_SIZE];
        uint16_t output_len = 0;
        status = SPStore_Get(&store, rid_b, output, sizeof(output), &output_len);
        if (status != SP_ERR_INVALID_SLOT)
            return fail(&store, "deleted RID is rejected before slot reuse", path);
    }

    if (SPStore_CompactPage(&store, page_b) != SP_OK)
        return fail(&store, "PF-backed explicit page compaction", path);

    if (SPStore_Close(&store) != SPSTORE_OK)
        return fail(&store, "close after delete/compact", path);

    if (SPStore_Open(&store, path) != SPSTORE_OK)
        return fail(&store, "reopen after delete/compact", path);

    if (!check_record(&store, rid_a, a, sizeof(a)) ||
        !check_record(&store, rid_c, c, sizeof(c)))
        return fail(&store, "surviving records persist after compaction", path);

    {
        unsigned char output[SP_PAGE_SIZE];
        uint16_t output_len = 0;
        status = SPStore_Get(&store, rid_b, output, sizeof(output), &output_len);
        if (status != SP_ERR_INVALID_SLOT)
            return fail(&store, "tombstone persists after reopening", path);
    }

    if (SPStore_Insert(&store, d, sizeof(d), &rid_d) != SPSTORE_OK)
        return fail(&store, "insert replacement after reopen", path);

    if (rid_d != rid_b)
        return fail(&store, "replacement reuses deleted slot RID", path);

    if (!check_record(&store, rid_a, a, sizeof(a)) ||
        !check_record(&store, rid_c, c, sizeof(c)) ||
        !check_record(&store, rid_d, d, sizeof(d)))
        return fail(&store, "records readable after slot reuse", path);

    if (SPStore_Close(&store) != SPSTORE_OK)
        return fail(&store, "final close", path);

    if (SPStore_Open(&store, path) != SPSTORE_OK)
        return fail(&store, "final reopen", path);

    if (!check_record(&store, rid_a, a, sizeof(a)) ||
        !check_record(&store, rid_c, c, sizeof(c)) ||
        !check_record(&store, rid_d, d, sizeof(d)))
        return fail(&store, "final persistence verification", path);

    if (SPStore_Close(&store) != SPSTORE_OK)
        return fail(&store, "final close after verification", path);

    if (SPStore_DestroyFile(path) != SPSTORE_OK)
        return fail(NULL, "destroy temporary PF file", path);

    printf("PASS: PF-backed insertion and RID-based retrieval\n");
    printf("PASS: PF-backed logical deletion and explicit compaction\n");
    printf("PASS: deletion and compaction persist across close/reopen\n");
    printf("PASS: deleted slot and RID are reused after reopening\n");
    printf("PF Phase 3 mutation/persistence test passed\n");
    return EXIT_SUCCESS;
}
