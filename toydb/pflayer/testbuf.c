
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "pf.h"
#include "pftypes.h"

#define TEST_FILE "testbuf_file"

void check(int result, char *message)
{
    if (result != PFE_OK) {
        printf("FAILED: %s (error %d)\n", message, result);
        exit(1);
    }
}

void test_policy(int policy, int expected_victim)
{
    int fd, error;
    int pagenum, i;
    char *pagebuf;
    PFBufferStats stats;

    unlink(TEST_FILE);

    check(PF_CreateFile(TEST_FILE), "create file");

    fd = PF_OpenFile(TEST_FILE);
    if (fd < 0) {
        printf("FAILED: open file\n");
        exit(1);
    }

    check(PFbufSetPolicy(policy), "set replacement policy");

    /* Allocate three pages */
    for (i = 0; i < 3; i++) {
        check(PF_AllocPage(fd, &pagenum, &pagebuf),
              "allocate page");

        check(PF_UnfixPage(fd, pagenum, TRUE),
              "unfix allocated page");
    }

    /* Allocate fourth page, forcing an eviction */
    check(PF_AllocPage(fd, &pagenum, &pagebuf),
          "allocate fourth page");

    check(PF_UnfixPage(fd, pagenum, TRUE),
          "unfix fourth page");

    /* Check whether the expected victim was evicted */
    PFbufResetStats();

    check(PF_GetThisPage(fd, expected_victim, &pagebuf),
          "fetch expected victim");

    PFbufGetStats(&stats);

    if (stats.physicalReads != 1) {
        printf("FAILED: replacement policy %d\n", policy);
        printf("Expected victim page %d was not evicted\n",
               expected_victim);
        exit(1);
    }

    check(PF_UnfixPage(fd, expected_victim, FALSE),
          "unfix fetched page");

    check(PF_CloseFile(fd), "close file");
    check(PF_DestroyFile(TEST_FILE), "destroy file");

    printf("PASS: replacement policy %d\n", policy);
}

int main()
{
    int fd, error;
    int pagenum, i;
    char *pagebuf;
    PFBufferStats stats;

    printf("\n===== BUFFER MANAGER TESTS =====\n");

    PF_Init();

    /* Test buffer capacity */
    check(PFbufSetCapacity(3), "set capacity to 3");

    printf("PASS: buffer capacity configured\n");

    /* Create and open test file */
    check(PF_CreateFile(TEST_FILE), "create file");

    fd = PF_OpenFile(TEST_FILE);
    if (fd < 0) {
        printf("FAILED: open file\n");
        return 1;
    }

    /* Fill all three buffer frames */
    for (i = 0; i < 3; i++) {
        check(PF_AllocPage(fd, &pagenum, &pagebuf),
              "allocate page");
    }

    /* All pages are fixed, so allocation must fail */
    error = PF_AllocPage(fd, &pagenum, &pagebuf);

    if (error != PFE_NOBUF) {
        printf("FAILED: expected PFE_NOBUF, got %d\n", error);
        return 1;
    }

    printf("PASS: all-fixed buffer protection\n");

    /* Unfix all three pages */
    for (i = 0; i < 3; i++) {
        check(PF_UnfixPage(fd, i, TRUE), "unfix page");
    }

    /* Test buffer hits and logical reads */
    PFbufResetStats();

    for (i = 0; i < 2; i++) {
        check(PF_GetThisPage(fd, 0, &pagebuf),
              "get existing page");

        check(PF_UnfixPage(fd, 0, FALSE),
              "unfix existing page");
    }

    PFbufGetStats(&stats);

    if (stats.logicalReads != 2 ||
        stats.bufferHits != 2 ||
        stats.physicalReads != 0) {
        printf("FAILED: buffer hit/statistics test\n");
        return 1;
    }

    printf("PASS: logical reads and buffer hits\n");

    /* Test dirty eviction and physical writes */
    PFbufResetStats();

    check(PF_AllocPage(fd, &pagenum, &pagebuf),
          "allocate page to force eviction");

    check(PF_UnfixPage(fd, pagenum, TRUE),
          "unfix newly allocated page");

    PFbufGetStats(&stats);

    if (stats.physicalWrites < 1) {
        printf("FAILED: dirty page was not written during eviction\n");
        return 1;
    }

    printf("PASS: dirty page eviction\n");

    /* Closing should flush remaining dirty pages */
    check(PF_CloseFile(fd), "close file");

    PFbufGetStats(&stats);

    if (stats.physicalWrites < 3) {
        printf("FAILED: dirty pages were not flushed on close\n");
        return 1;
    }

    printf("PASS: dirty page flushing on close\n");

    check(PF_DestroyFile(TEST_FILE), "destroy file");

    /* Test LRU: page 0 should be evicted */
    test_policy(PF_LRU, 0);

    /* Test MRU: page 2 should be evicted */
    test_policy(PF_MRU, 2);

    /* Test invalid configuration values */
    if (PFbufSetCapacity(0) == PFE_OK) {
        printf("FAILED: invalid capacity accepted\n");
        return 1;
    }

    if (PFbufSetPolicy(10) == PFE_OK) {
        printf("FAILED: invalid policy accepted\n");
        return 1;
    }

    printf("PASS: invalid configuration rejected\n");

    /* Reset statistics */
    PFbufResetStats();
    PFbufGetStats(&stats);

    if (stats.logicalReads != 0 ||
        stats.logicalWrites != 0 ||
        stats.physicalReads != 0 ||
        stats.physicalWrites != 0 ||
        stats.bufferHits != 0) {
        printf("FAILED: statistics reset\n");
        return 1;
    }

    printf("PASS: statistics reset\n");

    printf("\nALL BUFFER MANAGER TESTS PASSED\n");

    return 0;
}
