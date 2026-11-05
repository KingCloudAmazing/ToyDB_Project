#include <stdio.h>
#include <time.h>
#include <string.h>
#include "am.h"
#include "testam.h"
#include "pf_api.h"
#include "../pflayer/pftypes.h"

extern int xPF_OpenFile(char *fname);
extern int xPF_CloseFile(int fd);
extern int xAM_OpenIndexScan(int fd, char attrtype, int attrlen, int op, char *value);
extern int xAM_CloseIndexScan(int sd);
extern int xAM_FindNextEntry(int sd);

int main(int argc, char **argv)
{
    int fd, sd, rid, count = 0;
    char key[9] = "97305818";
    clock_t start, end;
    PFBufferStats stats;
    double elapsed;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s INDEX_FILE METHOD\n", argv[0]);
        return 2;
    }

    PF_Init();
    fd = xPF_OpenFile(argv[1]);
    if (fd < 0) return 1;

    PFbufResetStats();
    start = clock();

    sd = xAM_OpenIndexScan(fd, CHAR_TYPE, 8, EQ_OP, key);
    if (sd < 0) return 1;

    while ((rid = xAM_FindNextEntry(sd)) != AME_EOF) {
        if (rid < 0) return 1;
        count++;
    }
    xAM_CloseIndexScan(sd);

    end = clock();
    elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    PFbufGetStats(&stats);

    if (count != 2) {
        fprintf(stderr, "Expected 2 duplicate records, found %d\n", count);
        xPF_CloseFile(fd);
        return 1;
    }

    printf("%s,%.9f,%d,%ld,%ld,%ld,%ld,%ld\n",
           argv[2], elapsed, count,
           stats.logicalReads, stats.physicalReads,
           stats.logicalWrites, stats.physicalWrites,
           stats.bufferHits);

    xPF_CloseFile(fd);
    return 0;
}
