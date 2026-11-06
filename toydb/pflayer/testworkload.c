#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "pf.h"
#include "pftypes.h"

#define TEST_FILE "testworkload_file"
#define RESULT_FILE "pf_experiments/pf_workload_results.csv"

#define NUM_PAGES 5
#define NUM_OPERATIONS 20
#define BUFFER_CAPACITY 3

void check(int result, char *message)
{
    if (result != PFE_OK) {
        printf("FAILED: %s (error %d)\n", message, result);
        exit(1);
    }
}

int run_experiment(int policy, int write_count,
                   const char *workload_name, FILE *csv)
{
    int fd, i, pagenum;
    int accumulator;
    int is_write;
    int sequence[NUM_OPERATIONS];
    char *pagebuf;
    PFBufferStats stats;

    /* Use the same five-page access pattern in every experiment. */
    for (i = 0; i < NUM_OPERATIONS; i++) {
        sequence[i] = i % NUM_PAGES;
    }

    check(PFbufSetCapacity(BUFFER_CAPACITY),
          "set buffer capacity");

    check(PFbufSetPolicy(policy),
          "set replacement policy");

    unlink(TEST_FILE);

    check(PF_CreateFile(TEST_FILE),
          "create experiment file");

    fd = PF_OpenFile(TEST_FILE);

    if (fd < 0) {
        printf("FAILED: open experiment file\n");
        return 1;
    }

    /* Create five pages before measuring the workload. */
    for (i = 0; i < NUM_PAGES; i++) {
        check(PF_AllocPage(fd, &pagenum, &pagebuf),
              "allocate page");

        *((int *)pagebuf) = 0;

        check(PF_UnfixPage(fd, pagenum, TRUE),
              "unfix allocated page");
    }

    /* Flush initialization pages and remove them from the buffer. */
    check(PF_CloseFile(fd), "close initialization file");

    fd = PF_OpenFile(TEST_FILE);

    if (fd < 0) {
        printf("FAILED: reopen experiment file\n");
        return 1;
    }

    /* Start counting only the measured workload. */
    PFbufResetStats();

    accumulator = 0;

    for (i = 0; i < NUM_OPERATIONS; i++) {

        /*
         * Distribute writes evenly across the workload.
         * Exactly write_count operations will be writes.
         */
        accumulator += write_count;

        is_write = 0;

        if (accumulator >= NUM_OPERATIONS) {
            is_write = 1;
            accumulator -= NUM_OPERATIONS;
        }

        check(PF_GetThisPage(fd, sequence[i], &pagebuf),
              "get workload page");

        if (is_write) {
            *((int *)pagebuf) = i + 100;
            check(PF_UnfixPage(fd, sequence[i], TRUE),
                  "unfix dirty workload page");
        }
        else {
            check(PF_UnfixPage(fd, sequence[i], FALSE),
                  "unfix read-only workload page");
        }
    }

    /*
     * Close the file before collecting statistics.
     * This includes physical writes caused by dirty-page flushing.
     */
    check(PF_CloseFile(fd), "close workload file");

    PFbufGetStats(&stats);

    printf("%-8s %-8s %-8ld %-8ld %-8ld %-8ld %-8ld %.2f%%\n",
           workload_name,
           policy == PF_LRU ? "LRU" : "MRU",
           stats.logicalReads,
           stats.logicalWrites,
           stats.physicalReads,
           stats.physicalWrites,
           stats.bufferHits,
           stats.logicalReads == 0 ? 0.0 :
           (double)stats.bufferHits / stats.logicalReads * 100.0);

    fprintf(csv,
            "%s,%d,%d,%s,%d,%ld,%ld,%ld,%ld,%ld,%.2f\n",
            workload_name,
            NUM_OPERATIONS - write_count,
            write_count,
            policy == PF_LRU ? "LRU" : "MRU",
            BUFFER_CAPACITY,
            stats.logicalReads,
            stats.logicalWrites,
            stats.physicalReads,
            stats.physicalWrites,
            stats.bufferHits,
            stats.logicalReads == 0 ? 0.0 :
            (double)stats.bufferHits / stats.logicalReads * 100.0);

    check(PF_DestroyFile(TEST_FILE), "destroy experiment file");

    return 0;
}

int main()
{
    int i, policy;
    int write_counts[5] = {0, 5, 10, 15, 20};
    char *workload_names[5] = {
        "100R_0W",
        "75R_25W",
        "50R_50W",
        "25R_75W",
        "0R_100W"
    };

    FILE *csv;

    PF_Init();

    if (mkdir("pf_experiments", 0755) < 0 && errno != EEXIST) {
        printf("FAILED: could not create output directory: pf_experiments\n");
        return 1;
    }

    csv = fopen(RESULT_FILE, "w");

    if (csv == NULL) {
        printf("FAILED: could not create CSV file: %s\n", RESULT_FILE);
        return 1;
    }

    fprintf(csv,
            "workload,read_operations,write_operations,policy,"
            "capacity,logical_reads,logical_writes,physical_reads,"
            "physical_writes,buffer_hits,hit_ratio\n");

    printf("\n===== READ/WRITE WORKLOAD EXPERIMENTS =====\n");
    printf("Buffer capacity: %d pages\n", BUFFER_CAPACITY);
    printf("Operations per workload: %d\n\n", NUM_OPERATIONS);

    printf("%-8s %-8s %-8s %-8s %-8s %-8s %-8s %s\n",
           "Workload", "Policy", "LogReads", "LogWrites",
           "PhysReads", "PhysWrites", "Hits", "HitRatio");

    for (i = 0; i < 5; i++) {
        for (policy = PF_LRU; policy <= PF_MRU; policy++) {
            if (run_experiment(policy, write_counts[i],
                               workload_names[i], csv) != 0) {
                fclose(csv);
                return 1;
            }
        }
    }

    fclose(csv);

    printf("\nPASS: All read/write workload experiments completed.\n");
    printf("CSV saved to: %s\n", RESULT_FILE);

    return 0;}