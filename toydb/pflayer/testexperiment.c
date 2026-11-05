
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "pf.h"
#include "pftypes.h"

#define TEST_FILE "testexperiment_file"

/* Check whether a PF operation completed successfully */
void check(int result, char *message)
{
    if (result != PFE_OK) {
        printf("FAILED: %s (error %d)\n", message, result);
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    int fd, i, pagenum;
    int capacity, policy;
    char *pagebuf;
    PFBufferStats stats;

    /* Same page-access sequence for every experiment */
    int sequence[] = {0, 1, 2, 3, 0, 1, 4, 0, 1, 2, 3, 4};
    int n = sizeof(sequence) / sizeof(sequence[0]);

    /* Read capacity and policy from command-line arguments */
    if (argc != 3) {
        printf("Usage: ./testexperiment <capacity> <policy>\n");
        printf("Policy: 0 = LRU, 1 = MRU\n");
        return 1;
    }

    capacity = atoi(argv[1]);
    policy = atoi(argv[2]);

    /* Initialize the PF system */
    PF_Init();

    /* Set capacity and policy before allocating any buffer frames */
    check(PFbufSetCapacity(capacity), "set buffer capacity");
    check(PFbufSetPolicy(policy), "set replacement policy");

    /* Remove any old experiment file */
    unlink(TEST_FILE);

    /* Create a fresh file for this experiment */
    check(PF_CreateFile(TEST_FILE), "create experiment file");

    fd = PF_OpenFile(TEST_FILE);

    if (fd < 0) {
        printf("FAILED: open experiment file\n");
        return 1;
    }

    /* Create five pages so that every page access is valid */
    for (i = 0; i < 5; i++) {
        check(PF_AllocPage(fd, &pagenum, &pagebuf),
              "allocate page");

        check(PF_UnfixPage(fd, pagenum, TRUE),
              "unfix allocated page");
    }

    /* Close the file to flush and release initialization buffers */
    check(PF_CloseFile(fd), "close initialization file");

    /* Reopen the file before starting the measured workload */
    fd = PF_OpenFile(TEST_FILE);

    if (fd < 0) {
        printf("FAILED: reopen experiment file\n");
        return 1;
    }

    /* Reset counters so only workload operations are measured */
    PFbufResetStats();

    /* Execute the same page-access sequence */
    for (i = 0; i < n; i++) {
        check(PF_GetThisPage(fd, sequence[i], &pagebuf),
              "read page");

        check(PF_UnfixPage(fd, sequence[i], FALSE),
              "unfix page");
    }

    /* Collect buffer statistics after the workload */
    PFbufGetStats(&stats);

    /* Print results for this configuration */
    printf("%-8s %-10d %-12ld %-14ld %-12ld %-14ld %.2f%%\n",
           policy == PF_LRU ? "LRU" : "MRU",
           capacity,
           stats.logicalReads,
           stats.physicalReads,
           stats.bufferHits,
           stats.physicalWrites,
           stats.logicalReads == 0 ? 0.0 :
           (double)stats.bufferHits / stats.logicalReads * 100.0);

    /* Close the file to release its buffer frames */
    check(PF_CloseFile(fd), "close experiment file");

    /* Destroy the temporary experiment file */
    check(PF_DestroyFile(TEST_FILE), "destroy experiment file");

    return 0;
}
