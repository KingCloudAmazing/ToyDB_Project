
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "pf.h"
#include "pftypes.h"

#define TEST_FILE "teststats_file"

void check(int result, char *message)
{
    if (result != PFE_OK) {
        printf("FAILED: %s (error %d)\n", message, result);
        exit(1);
    }
}

void print_stats(PFBufferStats stats)
{
    printf("\nBuffer statistics:\n");
    printf("Logical reads   : %ld\n", stats.logicalReads);
    printf("Logical writes  : %ld\n", stats.logicalWrites);
    printf("Physical reads  : %ld\n", stats.physicalReads);
    printf("Physical writes : %ld\n", stats.physicalWrites);
    printf("Buffer hits     : %ld\n", stats.bufferHits);
}

int main()
{
    int fd, pagenum;
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

    /* Allocate one page and write to it */
    check(PF_AllocPage(fd, &pagenum, &pagebuf),
          "allocate page");

    *((int *)pagebuf) = 100;

    check(PF_UnfixPage(fd, pagenum, TRUE),
          "unfix dirty page");

    /* Close file to flush and remove its pages from buffer */
    check(PF_CloseFile(fd), "close file");

    /* Reset counters before measuring reads */
    PFbufResetStats();

    /* Reopen file and read page 0 from disk */
    fd = PF_OpenFile(TEST_FILE);
    if (fd < 0) {
        printf("FAILED: reopen file\n");
        return 1;
    }

    check(PF_GetThisPage(fd, 0, &pagebuf),
          "read page from disk");

    check(PF_UnfixPage(fd, 0, FALSE),
          "unfix page");

    /* Read the same page again: this should be a buffer hit */
    check(PF_GetThisPage(fd, 0, &pagebuf),
          "read page from buffer");

    check(PF_UnfixPage(fd, 0, TRUE),
          "unfix dirty page");

    PFbufGetStats(&stats);
    print_stats(stats);

    /* Expected:
       2 logical reads
       1 logical write
       1 physical read
       1 buffer hit
    */
    if (stats.logicalReads != 2 ||
        stats.logicalWrites != 1 ||
        stats.physicalReads != 1 ||
        stats.bufferHits != 1) {
        printf("\nFAILED: read/write counter verification\n");
        return 1;
    }

    printf("\nPASS: logical reads, logical writes, physical reads and hits\n");

    /* Close file: the dirty page must be written to disk */
    check(PF_CloseFile(fd), "close file and flush");

    PFbufGetStats(&stats);

    if (stats.physicalWrites != 1) {
        printf("FAILED: expected 1 physical write, got %ld\n",
               stats.physicalWrites);
        return 1;
    }

    printf("PASS: physical write on file close\n");

    /* Verify statistics reset */
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

    check(PF_DestroyFile(TEST_FILE), "destroy file");

    printf("\nALL STATISTICS TESTS PASSED\n");

    return 0;
}
