#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "pf.h"
#include "pftypes.h"

#define TEST_FILE "testlru_file"

void check(int result, char *message)
{
    if (result != PFE_OK) {
        printf("FAILED: %s (error %d)\n", message, result);
        exit(1);
    }
}

int main()
{
    int fd, i, pagenum;
    char *pagebuf;
    PFBufferStats stats;

    PF_Init();
    PFbufSetCapacity(3);
    PFbufSetPolicy(PF_LRU);

    unlink(TEST_FILE);

    check(PF_CreateFile(TEST_FILE), "create file");

    fd = PF_OpenFile(TEST_FILE);
    if (fd < 0) {
        printf("FAILED: open file\n");
        return 1;
    }

    /* Fill buffer with pages 0, 1 and 2 */
    for (i = 0; i < 3; i++) {
        check(PF_AllocPage(fd, &pagenum, &pagebuf),
              "allocate page");

        check(PF_UnfixPage(fd, pagenum, FALSE),
              "unfix page");
    }

    PFbufResetStats();

    /* Access page 0 again: it should become most recently used */
    check(PF_GetThisPage(fd, 0, &pagebuf),
          "access page 0");

    check(PF_UnfixPage(fd, 0, FALSE),
          "unfix page 0");

    /* Allocate page 3, forcing an eviction */
    check(PF_AllocPage(fd, &pagenum, &pagebuf),
          "allocate page 3");

    check(PF_UnfixPage(fd, pagenum, FALSE),
          "unfix page 3");

    PFbufResetStats();

    /* Page 1 should have been evicted */
    check(PF_GetThisPage(fd, 1, &pagebuf),
          "access page 1");

    check(PF_UnfixPage(fd, 1, FALSE),
          "unfix page 1");

    PFbufGetStats(&stats);

    printf("\nPhysical reads after accessing page 1: %ld\n",
           stats.physicalReads);

    if (stats.physicalReads == 1) {
        printf("PASS: page 1 was evicted after page 0 was accessed\n");
    } else {
        printf("FAILED: expected page 1 to be evicted\n");
        return 1;
    }

    check(PF_CloseFile(fd), "close file");
    check(PF_DestroyFile(TEST_FILE), "destroy file");

    printf("\nSTRICT LRU TEST PASSED\n");

    return 0;
}