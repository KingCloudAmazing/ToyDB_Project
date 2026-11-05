
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <unistd.h>

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

/* Construct sorted leaf pages without splitting a duplicate-key group. */
int buildLeafPages(int indexFd, LeafInfo *leaves,
                   int total, int *leafCount)
{
    int i = 0, j;
    int pageNum, err;
    int count = 0;
    int index;
    int groupEnd, groupSize, k;
    int required, available;

    char *pageBuf;
    char *prevBuf = NULL;
    int prevPage = -1;

    if (leaves == NULL || leafCount == NULL || total <= 0)
        return -1;

    *leafCount = 0;

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

        {
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
                /*
                 * Find the complete run of identical keys beginning at j.
                 * A key and all its RIDs must be stored in the same leaf.
                 */
                groupEnd = j + 1;
                while (groupEnd < total &&
                       memcmp(entries[j].roll,
                              entries[groupEnd].roll,
                              ROLL_LENGTH) == 0) {
                    groupEnd++;
                }

                groupSize = groupEnd - j;

                /*
                 * A new distinct key consumes:
                 *   key bytes + head pointer + first RID-list node.
                 * Each additional RID for this key consumes one RID-list
                 * node (int RID + short next pointer).
                 *
                 * New pages have an empty freelist, so this is the exact
                 * space requirement for the complete key group.
                 */
                required = ROLL_LENGTH + AM_ss +
                           groupSize * (AM_si + AM_ss);
                available = (int)header->recIdPtr -
                            (int)header->keyPtr;

                if (required > available) {
                    if (j == i) {
                        /*
                         * The group cannot fit even on an empty leaf.
                         * Do not split it, because doing so would create
                         * duplicate separator keys in adjacent leaves.
                         */
                        printf(
                            "Duplicate-key group of %d RIDs for key %.*s "
                            "cannot fit in an empty leaf page\n",
                            groupSize, ROLL_LENGTH, entries[j].roll
                        );
                        PF_UnfixPage(indexFd, pageNum, FALSE);
                        return -1;
                    }

                    /* Finish this leaf; retry the entire group on next leaf. */
                    break;
                }

                /* Every group placed in this leaf is a new distinct key. */
                index = header->numKeys + 1;

                err = AM_InsertintoLeaf(
                    pageBuf,
                    ROLL_LENGTH,
                    entries[j].roll,
                    entries[j].rid,
                    index,
                    AM_NOT_FOUND
                );

                if (!err) {
                    printf("Leaf insertion failed for key %.*s\n",
                           ROLL_LENGTH, entries[j].roll);
                    PF_UnfixPage(indexFd, pageNum, FALSE);
                    return -1;
                }

                /*
                 * Insert the remaining RIDs of this same key as duplicates.
                 * AM_FOUND prepends each RID to the leaf's RID linked list.
                 */
                for (k = j + 1; k < groupEnd; k++) {
                    err = AM_InsertintoLeaf(
                        pageBuf,
                        ROLL_LENGTH,
                        entries[k].roll,
                        entries[k].rid,
                        index,
                        AM_FOUND
                    );

                    if (!err) {
                        printf(
                            "Duplicate RID insertion failed for key %.*s "
                            "at sorted entry %d\n",
                            ROLL_LENGTH, entries[k].roll, k
                        );
                        PF_UnfixPage(indexFd, pageNum, FALSE);
                        return -1;
                    }
                }

                /* Advance only after the entire duplicate group is inserted. */
                j = groupEnd;
            }
        }

        if (j == i) {
            printf("No complete key group could fit in leaf page %d\n",
                   pageNum);
            PF_UnfixPage(indexFd, pageNum, FALSE);
            return -1;
        }

        /* Link previous leaf to current leaf. */
        if (prevBuf != NULL) {
            AM_LEAFHEADER *prevHeader =
                (AM_LEAFHEADER *)prevBuf;

            prevHeader->nextLeafPage = pageNum;

            if (PF_UnfixPage(indexFd, prevPage, TRUE) < 0) {
                printf("Error unfixing previous leaf page\n");
                PF_UnfixPage(indexFd, pageNum, FALSE);
                return -1;
            }
        }

        prevBuf = pageBuf;
        prevPage = pageNum;

        count++;
        i = j;
    }

    if (prevBuf != NULL) {
        if (PF_UnfixPage(indexFd, prevPage, TRUE) < 0) {
            printf("Error writing last leaf page\n");
            return -1;
        }
    }

    *leafCount = count;
    return 0;
}

/* Return the safe number of separator keys in one internal page. */
static int getInternalMaxKeys(void)
{
    int maxKeys;

    maxKeys = (PF_PAGE_SIZE - AM_sint - AM_si) /
              (AM_si + ROLL_LENGTH);

    /* Preserve the even-capacity convention used by the original AM code. */
    if (maxKeys % 2 != 0)
        maxKeys--;

    if (maxKeys < 1 || maxKeys > SHRT_MAX)
        return -1;

    return maxKeys;
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

    maxKeys = getInternalMaxKeys();

    if (leaves == NULL || leafCount <= 0 || maxKeys < 1) {
        printf("Invalid leaf list or internal-page capacity\n");
        return -1;
    }

    if (leafCount - 1 > maxKeys ||
        AM_sint + AM_si +
            (leafCount - 1) * (ROLL_LENGTH + AM_si) > PF_PAGE_SIZE) {
        printf("Root capacity exceeded: requested %d children; maximum is %d\n",
               leafCount, maxKeys + 1);
        return -1;
    }

    err = PF_GetThisPage(indexFd, 0, &pageBuf);

    if (err < 0) {
        printf("Error accessing root page\n");
        return -1;
    }

    memset(pageBuf, 0, PF_PAGE_SIZE);

    memset(&header, 0, sizeof(header));
    header.pageType = 'i';
    header.numKeys = (short)(leafCount - 1);
    header.maxKeys = (short)maxKeys;
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

/* Verify an ALL scan begins at the actual leftmost leaf and sees every RID. */
static int verifyAllScan(int indexFd, int expected)
{
    int scanDesc;
    int rid;
    int count = 0;

    scanDesc = xAM_OpenIndexScan(indexFd, CHAR_TYPE, ROLL_LENGTH, ALL, NULL);
    if (scanDesc < 0) {
        printf("Could not open ALL index scan\n");
        return -1;
    }

    while ((rid = xAM_FindNextEntry(scanDesc)) != AME_EOF) {
        if (rid < 0) {
            printf("ALL scan failed with error %d\n", rid);
            (void)xAM_CloseIndexScan(scanDesc);
            return -1;
        }
        count++;
    }

    if (xAM_CloseIndexScan(scanDesc) < 0) {
        printf("Could not close ALL index scan\n");
        return -1;
    }

    printf("ALL scan expected records : %d\n", expected);
    printf("ALL scan actual records   : %d\n", count);
    return count == expected ? 0 : -1;
}

/*
 * Synthetic regression for duplicate groups at a leaf-page boundary.
 *
 * Fill the first leaf with distinct one-RID keys until the remaining space
 * can hold one more distinct key but cannot hold the complete duplicate
 * group. The complete duplicate group must start on the next leaf.
 */
static int runDuplicateBoundaryRegression(void)
{
    const char *indexName = "student_bulk_boundary";
    const char *indexFile = "student_bulk_boundary.0";
    const int duplicateGroupSize = 4;
    const int singleKeyBytes =
        ROLL_LENGTH + AM_ss + AM_si + AM_ss;
    const int duplicateGroupBytes =
        ROLL_LENGTH + AM_ss +
        duplicateGroupSize * (AM_si + AM_ss);

    LeafInfo leaves[MAX_STUDENTS];
    int payloadBytes;
    int singles;
    int remaining;
    int total;
    int i;
    int leafCount = 0;
    int pageNum;
    int indexFd = -1;
    int err;
    char *pageBuf;
    char keyBuffer[ROLL_LENGTH + 1];
    char duplicateKey[ROLL_LENGTH] = {
        '9', '9', '9', '9', '9', '9', '9', '9'
    };

    printf("\n========== SYNTHETIC DUPLICATE-BOUNDARY TEST ==========\n");

    /*
     * Derive the boundary from the actual AM header size and ABI. Find a
     * number of distinct keys that leaves enough room for a singleton key
     * but less room than the complete duplicate group requires.
     */
    payloadBytes = PF_PAGE_SIZE - AM_sl;

    for (singles = 1;
         singles < MAX_STUDENTS - duplicateGroupSize;
         ++singles) {
        remaining = payloadBytes - singles * singleKeyBytes;

        if (remaining >= singleKeyBytes &&
            remaining < duplicateGroupBytes) {
            break;
        }
    }

    if (singles >= MAX_STUDENTS - duplicateGroupSize) {
        printf("Could not construct a duplicate-group boundary workload\n");
        return EXIT_FAILURE;
    }

    remaining = payloadBytes - singles * singleKeyBytes;
    total = singles + duplicateGroupSize;

    /* Prepare sorted, distinct keys preceding the duplicate group. */
    for (i = 0; i < singles; ++i) {
        (void)snprintf(keyBuffer, sizeof(keyBuffer), "%08d", i + 1);
        memcpy(entries[i].roll, keyBuffer, ROLL_LENGTH);
        entries[i].rid = 100000 + i;
    }

    /* Add one duplicate key with four distinct synthetic RIDs. */
    for (i = 0; i < duplicateGroupSize; ++i) {
        memcpy(entries[singles + i].roll,
               duplicateKey, ROLL_LENGTH);
        entries[singles + i].rid = 900000 + i;
    }

    printf("Distinct keys before duplicate group : %d\n", singles);
    printf("Duplicate-group size                 : %d\n",
           duplicateGroupSize);
    printf("Free bytes before duplicate group    : %d\n", remaining);
    printf("Bytes required by duplicate group    : %d\n",
           duplicateGroupBytes);

    if (remaining < singleKeyBytes ||
        remaining >= duplicateGroupBytes) {
        printf("FAIL: synthetic workload did not reach the intended boundary\n");
        return EXIT_FAILURE;
    }

    PF_Init();

    /*
     * The xAM_DestroyIndex wrapper exits when the file does not exist.
     * Check first so a fresh checkout can run this test safely.
     */
    if (access(indexFile, F_OK) == 0) {
        err = AM_DestroyIndex((char *)indexName, 0);
        if (err != AME_OK) {
            printf("Could not remove previous synthetic index: %d\n", err);
            return EXIT_FAILURE;
        }
    }

    err = xAM_CreateIndex((char *)indexName, 0, CHAR_TYPE, ROLL_LENGTH);
    if (err < 0) {
        printf("Could not create synthetic boundary index: %d\n", err);
        return EXIT_FAILURE;
    }

    indexFd = xPF_OpenFile((char *)indexFile);

    /* The first allocated leaf now follows root page 0 directly. */

    err = buildLeafPages(indexFd, leaves, total, &leafCount);
    if (err < 0) {
        printf("FAIL: synthetic leaf construction failed\n");
        (void)xPF_CloseFile(indexFd);
        (void)xAM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }

    if (leafCount != 2) {
        printf("FAIL: expected exactly two leaf pages, got %d\n",
               leafCount);
        (void)xPF_CloseFile(indexFd);
        (void)xAM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }

    if (memcmp(leaves[1].firstKey,
               duplicateKey, ROLL_LENGTH) != 0) {
        printf("FAIL: duplicate group did not begin at the second leaf\n");
        (void)xPF_CloseFile(indexFd);
        (void)xAM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }

    err = buildRootPage(indexFd, leaves, leafCount);
    if (err < 0) {
        printf("FAIL: synthetic root construction failed\n");
        (void)xPF_CloseFile(indexFd);
        (void)xAM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }

    if (xPF_CloseFile(indexFd) < 0) {
        printf("Could not close synthetic boundary index\n");
        (void)xAM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }
    indexFd = -1;

    /* Reopen and verify through the public AM equality-scan interface. */
    indexFd = xPF_OpenFile((char *)indexFile);

    if (verifySearch(indexFd, entries[singles - 1].roll, 1) < 0 ||
        verifySearch(indexFd, duplicateKey, duplicateGroupSize) < 0 ||
        verifyAllScan(indexFd, total) < 0) {
        printf("FAIL: synthetic duplicate-boundary search verification\n");
        (void)xPF_CloseFile(indexFd);
        (void)xAM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }

    if (xPF_CloseFile(indexFd) < 0) {
        printf("Could not close synthetic boundary index after verification\n");
        (void)xAM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }
    indexFd = -1;

    if (AM_DestroyIndex((char *)indexName, 0) != AME_OK) {
        printf("Could not remove synthetic boundary index after test\n");
        return EXIT_FAILURE;
    }

    printf("PASS: duplicate group was moved intact to the next leaf\n");
    printf("PASS: all %d RIDs for the boundary key were searchable\n",
           duplicateGroupSize);
    printf("PASS: synthetic duplicate-boundary regression\n");
    printf("PASS: ALL scan begins at the actual leftmost leaf\n");

    return EXIT_SUCCESS;
}

/*
 * Capacity regression for the deliberately supported one-level root.
 * The tree must accept the maximum representable leaf-child count and reject
 * one additional child before modifying the existing root page.
 */
static int runRootCapacityRegression(void)
{
    const char *indexName = "student_root_capacity";
    const char *indexFile = "student_root_capacity.0";
    LeafInfo leaves[MAX_STUDENTS];
    AM_INTHEADER rootHeader;
    AM_INTHEADER afterHeader;
    char rootSnapshot[PF_PAGE_SIZE];
    char keyBuffer[ROLL_LENGTH + 1];
    char *pageBuf;
    int maxKeys;
    int maxChildren;
    int i;
    int pageNum;
    int indexFd = -1;
    int err;

    printf("\n========== ROOT CAPACITY REGRESSION ==========\n");

    maxKeys = getInternalMaxKeys();
    if (maxKeys < 1 || maxKeys + 2 > MAX_STUDENTS) {
        printf("FAIL: invalid calculated internal-node capacity\n");
        return EXIT_FAILURE;
    }
    maxChildren = maxKeys + 1;

    printf("Internal-page separator capacity : %d\n", maxKeys);
    printf("Maximum leaf children            : %d\n", maxChildren);

    PF_Init();
    if (access(indexFile, F_OK) == 0) {
        err = AM_DestroyIndex((char *)indexName, 0);
        if (err != AME_OK) {
            printf("Could not remove previous capacity-test index: %d\n", err);
            return EXIT_FAILURE;
        }
    }

    err = xAM_CreateIndex((char *)indexName, 0, CHAR_TYPE, ROLL_LENGTH);
    if (err < 0) {
        printf("Could not create root-capacity test index: %d\n", err);
        return EXIT_FAILURE;
    }

    indexFd = xPF_OpenFile((char *)indexFile);
    if (indexFd < 0) {
        printf("Could not open root-capacity test index\n");
        (void)AM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }

    /* Allocate real child-page numbers for the exact-capacity and overflow cases. */
    for (i = 0; i < maxChildren + 1; ++i) {
        err = PF_AllocPage(indexFd, &pageNum, &pageBuf);
        if (err < 0) {
            printf("Could not allocate synthetic child page %d\n", i);
            (void)xPF_CloseFile(indexFd);
            (void)AM_DestroyIndex((char *)indexName, 0);
            return EXIT_FAILURE;
        }

        leaves[i].pageNum = pageNum;
        (void)snprintf(keyBuffer, sizeof(keyBuffer), "%08d", i + 1);
        memcpy(leaves[i].firstKey, keyBuffer, ROLL_LENGTH);

        if (PF_UnfixPage(indexFd, pageNum, FALSE) < 0) {
            printf("Could not unfix synthetic child page %d\n", i);
            (void)xPF_CloseFile(indexFd);
            (void)AM_DestroyIndex((char *)indexName, 0);
            return EXIT_FAILURE;
        }
    }

    /* Exactly maxKeys separators / maxChildren child pages must fit. */
    if (buildRootPage(indexFd, leaves, maxChildren) < 0) {
        printf("FAIL: root rejected its exact supported capacity\n");
        (void)xPF_CloseFile(indexFd);
        (void)AM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }

    err = PF_GetThisPage(indexFd, 0, &pageBuf);
    if (err < 0) {
        printf("Could not read constructed root page\n");
        (void)xPF_CloseFile(indexFd);
        (void)AM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }
    memcpy(&rootHeader, pageBuf, AM_sint);
    memcpy(rootSnapshot, pageBuf, PF_PAGE_SIZE);
    if (PF_UnfixPage(indexFd, 0, FALSE) < 0) {
        printf("Could not unfix constructed root page\n");
        (void)xPF_CloseFile(indexFd);
        (void)AM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }

    if (rootHeader.pageType != 'i' ||
        rootHeader.numKeys != maxKeys ||
        rootHeader.maxKeys != maxKeys ||
        rootHeader.attrLength != ROLL_LENGTH) {
        printf("FAIL: root header does not report the expected capacity\n");
        (void)xPF_CloseFile(indexFd);
        (void)AM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }
    printf("PASS: root accepts exactly %d child pages\n", maxChildren);

    /* One extra leaf child must be rejected without changing the root. */
    if (buildRootPage(indexFd, leaves, maxChildren + 1) == 0) {
        printf("FAIL: root accepted %d children (capacity is %d)\n",
               maxChildren + 1, maxChildren);
        (void)xPF_CloseFile(indexFd);
        (void)AM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }

    err = PF_GetThisPage(indexFd, 0, &pageBuf);
    if (err < 0) {
        printf("Could not reread root after overflow rejection\n");
        (void)xPF_CloseFile(indexFd);
        (void)AM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }
    memcpy(&afterHeader, pageBuf, AM_sint);
    i = memcmp(rootSnapshot, pageBuf, PF_PAGE_SIZE);
    if (PF_UnfixPage(indexFd, 0, FALSE) < 0) {
        printf("Could not unfix root after overflow check\n");
        (void)xPF_CloseFile(indexFd);
        (void)AM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }

    if (i != 0 || afterHeader.numKeys != maxKeys) {
        printf("FAIL: overflow rejection modified the existing root page\n");
        (void)xPF_CloseFile(indexFd);
        (void)AM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }
    printf("PASS: root rejects overflow without modifying the page\n");

    if (xPF_CloseFile(indexFd) < 0) {
        printf("Could not close root-capacity test index\n");
        (void)AM_DestroyIndex((char *)indexName, 0);
        return EXIT_FAILURE;
    }
    if (AM_DestroyIndex((char *)indexName, 0) != AME_OK) {
        printf("Could not destroy root-capacity test index\n");
        return EXIT_FAILURE;
    }

    printf("PASS: root-capacity regression (one-level-root limit documented)\n");
    return EXIT_SUCCESS;
}


int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--boundary-test") == 0)
        return runDuplicateBoundaryRegression();
    if (argc == 2 && strcmp(argv[1], "--capacity-test") == 0)
        return runRootCapacityRegression();

    if (argc != 1) {
        fprintf(stderr, "Usage: %s [--boundary-test | --capacity-test]\\n", argv[0]);
        return EXIT_FAILURE;
    }

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
    if (access("student_bulk.0", F_OK) == 0)
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

    /* First leaf allocation follows root page 0; no dummy page is needed. */

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
        verifySearch(indexFd, "99310001", 2) < 0 ||
        verifyAllScan(indexFd, total) < 0) {

        printf("FAIL: Bulk index search verification\n");
        xPF_CloseFile(indexFd);
        return 1;
    }

    printf("\nPASS: Bulk leaf construction\n");
    printf("PASS: Root internal page construction\n");
    printf("PASS: Duplicate roll-number searches\n");
    printf("PASS: ALL scan from dynamically discovered leftmost leaf\n");

    xPF_CloseFile(indexFd);

    printf("\n========== BULK LOAD COMPLETE ==========\n");

    return 0;
}
