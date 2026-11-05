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

int main(void)
{
    FILE *input;
    char line[MAX_LINE];
    SPStore store;
    int count = 0;
    int status, indexFd, scanDesc;
    char duplicateKey[ROLL_LENGTH] = "97305818";
    int duplicateCount = 0;
    int rid;

    input = fopen("../../data/student.txt", "r");
    if (input == NULL) {
        perror("student.txt");
        return 1;
    }

    /* Skip descriptive header. */
    if (fgets(line, sizeof(line), input) == NULL) {
        printf("Dataset is empty\n");
        fclose(input);
        return 1;
    }

    PF_Init();

    remove("student_incremental_records.dat");
    remove("student_incremental.0");

    status = SPStore_CreateFile("student_incremental_records.dat");
    if (status != SPSTORE_OK) {
        printf("Create failed: %s\n", SPStore_ErrorString(status));
        fclose(input);
        return 1;
    }

    status = SPStore_Open(&store, "student_incremental_records.dat");
    if (status != SPSTORE_OK) {
        printf("Open failed: %s\n", SPStore_ErrorString(status));
        fclose(input);
        return 1;
    }

    /*
     * Create an empty index before loading Student records.
     */
    xAM_CreateIndex("student_incremental", 0,
                    CHAR_TYPE, ROLL_LENGTH);

    indexFd = xPF_OpenFile("student_incremental.0");

    /*
     * Incremental construction:
     * Read one record, store it, and immediately index it.
     */
    while (fgets(line, sizeof(line), input) != NULL) {
        char *first;
        char *second;
        size_t length;
        size_t rollLength;

        length = strlen(line);

        while (length > 0 &&
              (line[length - 1] == '\n' ||
               line[length - 1] == '\r')) {
            line[--length] = '\0';
        }

        if (length == 0)
            continue;

        if (count >= MAX_STUDENTS) {
            printf("Student limit exceeded\n");
            fclose(input);
            return 1;
        }

        first = strchr(line, ';');
        if (first == NULL) {
            printf("Invalid Student record\n");
            fclose(input);
            return 1;
        }

        second = strchr(first + 1, ';');
        if (second == NULL) {
            printf("Invalid Student record\n");
            fclose(input);
            return 1;
        }

        rollLength = (size_t)(second - first - 1);

        if (rollLength != ROLL_LENGTH) {
            printf("Invalid roll number: %s\n", first + 1);
            fclose(input);
            return 1;
        }

        /*
         * Insert complete record into slotted-page storage.
         * Save the actual RID returned by the storage layer.
         */
        status = SPStore_Insert(&store, line,
                                (uint16_t)length, &rid);

        if (status != SPSTORE_OK) {
            printf("Storage insert failed: %s\n",
                   SPStore_ErrorString(status));
            fclose(input);
            return 1;
        }

        /*
         * Insert the roll number and its RID immediately.
         */
        xAM_InsertEntry(indexFd, CHAR_TYPE, ROLL_LENGTH,
                        first + 1, rid);

        count++;
    }

    fclose(input);

    printf("Student records loaded: %d\n", count);
    printf("Incremental index entries inserted: %d\n", count);

    /* Verify duplicate roll-number search. */
    scanDesc = xAM_OpenIndexScan(indexFd, CHAR_TYPE,
                                 ROLL_LENGTH, EQ_OP, duplicateKey);

    while (1) {
        int result = xAM_FindNextEntry(scanDesc);

        if (result == AME_EOF)
            break;

        if (result < 0) {
            printf("Unexpected scan error: %d\n", result);
            return 1;
        }

        printf("Matching RID: %d\n", result);
        duplicateCount++;
    }

    xAM_CloseIndexScan(scanDesc);
    xPF_CloseFile(indexFd);
    SPStore_Close(&store);

    printf("\n========== INCREMENTAL AM RESULT ==========\n");
    printf("Total Student records : %d\n", count);
    printf("Search roll number    : %.*s\n",
           ROLL_LENGTH, duplicateKey);
    printf("Matching records      : %d\n", duplicateCount);

    if (count != 17813 || duplicateCount != 2) {
        printf("FAIL: Incremental index verification\n");
        return 1;
    }

    printf("PASS: Incremental index construction\n");
    printf("PASS: Duplicate roll-number search\n");

    return 0;
}
