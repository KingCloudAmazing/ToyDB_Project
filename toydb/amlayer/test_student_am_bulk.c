
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "am.h"
#include "testam.h"
#include "student_store.h"
#include "pf_api.h"

#define MAX_STUDENTS 30000
#define MAX_LINE 4096
#define ROLL_LENGTH 8

typedef struct {
    char roll[ROLL_LENGTH];
    int rid;
} StudentEntry;

typedef struct {
    int pageNum;
    char firstKey[ROLL_LENGTH];
} LeafInfo;

StudentEntry entries[MAX_STUDENTS];

/* Compare entries using their roll numbers */
int compareEntries(const void *a, const void *b)
{
    const StudentEntry *x = (const StudentEntry *)a;
    const StudentEntry *y = (const StudentEntry *)b;

    return memcmp(x->roll, y->roll, ROLL_LENGTH);
}

/* Extract the second semicolon-separated field */
int extractRollNumber(char *line, char roll[ROLL_LENGTH + 1])
{
    char *firstSep;
    char *secondSep;
    int rollLen;

    firstSep = strchr(line, ';');
    if (firstSep == NULL)
        return 0;

    secondSep = strchr(firstSep + 1, ';');
    if (secondSep == NULL)
        return 0;

    rollLen = (int)(secondSep - firstSep - 1);

    if (rollLen != ROLL_LENGTH)
        return 0;

    memcpy(roll, firstSep + 1, ROLL_LENGTH);
    roll[ROLL_LENGTH] = '\0';

    return 1;
}

extern int AM_InsertintoLeaf(char *, int, char *, int, int, int);

/* Construct sorted leaf pages */
int buildLeafPages(int indexFd, LeafInfo *leaves,
                   int total, int *leafCount)
{
    int i = 0, j;
    int pageNum, err;
    int count = 0;
    int index, newKey, required;

    char *pageBuf;
    char *prevBuf = NULL;
    int prevPage = -1;

    while (i < total) {

        if (count >= MAX_STUDENTS) {
            printf("Too many leaf pages\n");
            return -1;
        }

        err = PF_AllocPage(indexFd, &pageNum, &pageBuf);

        if (err < 0) {
            printf("Error allocating leaf page\n");
            return -1;
        }

        memset(pageBuf, 0, PF_PAGE_SIZE);

        AM_LEAFHEADER *header = (AM_LEAFHEADER *)pageBuf;

        header->pageType = 'l';
        header->nextLeafPage = AM_NULL_PAGE;
        header->recIdPtr = PF_PAGE_SIZE;
        header->keyPtr = AM_sl;
        header->freeListPtr = AM_NULL;
        header->numinfreeList = 0;
        header->attrLength = ROLL_LENGTH;
        header->numKeys = 0;

        header->maxKeys =
            (PF_PAGE_SIZE - AM_sl) /
            (ROLL_LENGTH + AM_ss + AM_si + AM_ss);

        leaves[count].pageNum = pageNum;

        memcpy(leaves[count].firstKey,
               entries[i].roll, ROLL_LENGTH);

        j = i;

        while (j < total) {

            newKey = (j == i ||
                memcmp(entries[j].roll,
                       entries[j - 1].roll,
                       ROLL_LENGTH) != 0);

            if (newKey) {
                index = header->numKeys + 1;
                required = ROLL_LENGTH + AM_ss + AM_si + AM_ss;
            } else {
                index = header->numKeys;
                required = AM_si + AM_ss;
            }

            /* Check available space */
            if ((header->recIdPtr - header->keyPtr) < required)
                break;

            err = AM_InsertintoLeaf(
                pageBuf,
                ROLL_LENGTH,
                entries[j].roll,
                entries[j].rid,
                index,
                newKey ? AM_NOT_FOUND : AM_FOUND
            );

            if (!err) {
                printf("Leaf insertion failed at record %d\n", j);
                PF_UnfixPage(indexFd, pageNum, 0);
                return -1;
            }

            j++;
        }

        if (j == i) {
            printf("No record could fit in leaf page %d\n", pageNum);
            PF_UnfixPage(indexFd, pageNum, 0);
            return -1;
        }

        /* Link previous leaf to current leaf */
        if (prevBuf != NULL) {

            AM_LEAFHEADER *prevHeader =
                (AM_LEAFHEADER *)prevBuf;

            prevHeader->nextLeafPage = pageNum;

            if (PF_UnfixPage(indexFd, prevPage, 1) < 0) {
                printf("Error unfixing previous leaf page\n");
                PF_UnfixPage(indexFd, pageNum, 0);
                return -1;
            }
        }

        prevBuf = pageBuf;
        prevPage = pageNum;

        count++;
        i = j;
    }

    if (prevBuf != NULL) {
        if (PF_UnfixPage(indexFd, prevPage, 1) < 0) {
            printf("Error writing last leaf page\n");
            return -1;
        }
    }

    *leafCount = count;

    return 0;
}

/* Construct the root internal page */
int buildRootPage(int indexFd, LeafInfo *leaves, int leafCount)
{
    int i;
    int offset;
    int maxKeys;
    int err;

    char *pageBuf;
    AM_INTHEADER header;

    maxKeys = (PF_PAGE_SIZE - AM_sint - AM_si) /
              (AM_si + ROLL_LENGTH);

    if (maxKeys % 2 != 0)
        maxKeys--;

    if (leafCount <= 0 || leafCount - 1 > maxKeys) {
        printf("Invalid leaf count or root capacity exceeded\n");
        return -1;
    }

    err = PF_GetThisPage(indexFd, 0, &pageBuf);

    if (err < 0) {
        printf("Error accessing root page\n");
        return -1;
    }

    memset(pageBuf, 0, PF_PAGE_SIZE);

    header.pageType = 'i';
    header.numKeys = leafCount - 1;
    header.maxKeys = maxKeys;
    header.attrLength = ROLL_LENGTH;

    /* First child pointer */
    memcpy(pageBuf + AM_sint,
           &leaves[0].pageNum,
           AM_si);

    offset = AM_sint + AM_si;

    /*
       Internal page layout:
       Child0 | Key0 | Child1 | Key1 | Child2 ...
       Each separator is the first key of the right child.
    */
    for (i = 1; i < leafCount; i++) {

        memcpy(pageBuf + offset,
               leaves[i].firstKey,
               ROLL_LENGTH);

        offset += ROLL_LENGTH;

        memcpy(pageBuf + offset,
               &leaves[i].pageNum,
               AM_si);

        offset += AM_si;
    }

    memcpy(pageBuf, &header, AM_sint);

    if (PF_UnfixPage(indexFd, 0, 1) < 0) {
        printf("Error writing root page\n");
        return -1;
    }

    return 0;
}

/* Search for a key and verify the number of matching records */
int verifySearch(int indexFd, char *key, int expected)
{
    int scanDesc;
    int rid;
    int count = 0;

    scanDesc = xAM_OpenIndexScan(
        indexFd,
        CHAR_TYPE,
        ROLL_LENGTH,
        EQ_OP,
        key
    );

    if (scanDesc < 0) {
        printf("Could not open index scan\n");
        return -1;
    }

    while ((rid = xAM_FindNextEntry(scanDesc)) != AME_EOF) {

        if (rid < 0) {
            printf("Search error: %d\n", rid);
            xAM_CloseIndexScan(scanDesc);
            return -1;
        }

        printf("Matching RID: %d\n", rid);
        count++;
    }

    xAM_CloseIndexScan(scanDesc);

    printf("Search key       : %.*s\n", ROLL_LENGTH, key);
    printf("Expected matches : %d\n", expected);
    printf("Actual matches   : %d\n", count);

    return (count == expected) ? 0 : -1;
}

int main()
{
    FILE *fp;
    char line[MAX_LINE];
    char roll[ROLL_LENGTH + 1];

    int total = 0;
    int rid;
    int indexFd;
    int err;
    int leafCount = 0;

    LeafInfo leaves[MAX_STUDENTS];
    SPStore store;

    printf("\n========== BULK DATA PREPARATION ==========\n");

    /* Create Student record storage */
    SPStore_DestroyFile("student_bulk_records.dat");

    if (SPStore_CreateFile("student_bulk_records.dat") < 0) {
        printf("Could not create Student file\n");
        return 1;
    }

    if (SPStore_Open(&store, "student_bulk_records.dat") < 0) {
        printf("Could not open Student file\n");
        return 1;
    }

    fp = fopen("../../data/student.txt", "r");

    if (fp == NULL)
        fp = fopen("../data/student.txt", "r");

    if (fp == NULL) {
        printf("Could not open data/student.txt\n");
        SPStore_Close(&store);
        return 1;
    }

    /* Skip descriptive header */
    if (fgets(line, sizeof(line), fp) == NULL) {
        printf("Student data file is empty\n");
        fclose(fp);
        SPStore_Close(&store);
        return 1;
    }

    /* Load records and collect their RIDs */
    while (fgets(line, sizeof(line), fp) != NULL) {

        int len = strlen(line);

        while (len > 0 &&
              (line[len - 1] == '\n' ||
               line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        if (len == 0)
            continue;

        if (total >= MAX_STUDENTS) {
            printf("Maximum student limit reached\n");
            fclose(fp);
            SPStore_Close(&store);
            return 1;
        }

        /* Extract the actual roll number: second field */
        if (!extractRollNumber(line, roll)) {
            printf("Invalid Student record at line %d\n", total + 2);
            fclose(fp);
            SPStore_Close(&store);
            return 1;
        }

        err = SPStore_Insert(
            &store,
            line,
            (uint16_t)len,
            &rid
        );

        if (err < 0) {
            printf("Student insertion failed\n");
            fclose(fp);
            SPStore_Close(&store);
            return 1;
        }

        memcpy(entries[total].roll, roll, ROLL_LENGTH);
        entries[total].rid = rid;

        total++;
    }

    fclose(fp);
    SPStore_Close(&store);

    printf("Student records loaded : %d\n", total);

    /* Sort entries by roll number */
    qsort(entries, total, sizeof(StudentEntry), compareEntries);

    printf("Records sorted         : %d\n", total);

    printf("\n========== BULK LEAF CONSTRUCTION ==========\n");

    /* Create a fresh index */
    xAM_DestroyIndex("student_bulk", 0);

    err = xAM_CreateIndex(
        "student_bulk",
        0,
        CHAR_TYPE,
        ROLL_LENGTH
    );

    if (err < 0) {
        printf("Could not create index\n");
        return 1;
    }

    indexFd = xPF_OpenFile("student_bulk.0");

    if (indexFd < 0) {
        printf("Could not open index file\n");
        return 1;
    }

    /*
       Reserve page 1 because the existing AM scan code
       expects the leftmost leaf page to be page 2.
    */
    {
        int reservedPage;
        char *reservedBuf;

        err = PF_AllocPage(indexFd, &reservedPage, &reservedBuf);

        if (err < 0 || reservedPage != 1) {
            printf("Could not reserve page 1\n");
            xPF_CloseFile(indexFd);
            return 1;
        }

        memset(reservedBuf, 0, PF_PAGE_SIZE);

        if (PF_UnfixPage(indexFd, reservedPage, 1) < 0) {
            printf("Could not unfix reserved page\n");
            xPF_CloseFile(indexFd);
            return 1;
        }
    }

    /* Build leaf pages */
    err = buildLeafPages(indexFd, leaves, total, &leafCount);

    if (err < 0) {
        printf("Leaf construction failed\n");
        xPF_CloseFile(indexFd);
        return 1;
    }

    printf("Total Student records : %d\n", total);
    printf("Leaf pages created    : %d\n", leafCount);
    printf("First leaf page       : %d\n", leaves[0].pageNum);
    printf("Last leaf page        : %d\n",
           leaves[leafCount - 1].pageNum);

    printf("\n========== ROOT CONSTRUCTION ==========\n");

    /* Build root page 0 */
    err = buildRootPage(indexFd, leaves, leafCount);

    if (err < 0) {
        printf("Root construction failed\n");
        xPF_CloseFile(indexFd);
        return 1;
    }

    printf("Root page             : 0\n");
    printf("Separator keys        : %d\n", leafCount - 1);

    if (xPF_CloseFile(indexFd) < 0) {
        printf("Could not close index file\n");
        return 1;
    }

    printf("\n========== SEARCH VERIFICATION ==========\n");

    /* Reopen index for searches */
    indexFd = xPF_OpenFile("student_bulk.0");

    if (indexFd < 0) {
        printf("Could not reopen index file\n");
        return 1;
    }

    /* Verify known duplicate roll numbers */
    if (verifySearch(indexFd, "97305818", 2) < 0 ||
        verifySearch(indexFd, "99302001", 2) < 0 ||
        verifySearch(indexFd, "99310001", 2) < 0) {

        printf("FAIL: Bulk index search verification\n");
        xPF_CloseFile(indexFd);
        return 1;
    }

    printf("\nPASS: Bulk leaf construction\n");
    printf("PASS: Root internal page construction\n");
    printf("PASS: Duplicate roll-number searches\n");

    xPF_CloseFile(indexFd);

    printf("\n========== BULK LOAD COMPLETE ==========\n");

    return 0;
}
