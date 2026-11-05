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

struct StudentEntry {
    char roll[ROLL_LENGTH];
    int rid;
};

int main(void)
{
    FILE *input;
    char line[MAX_LINE];
    struct StudentEntry *entries;
    SPStore store;
    int count = 0;
    int i, status, indexFd, scanDesc;
    char duplicateKey[ROLL_LENGTH] = "97305818";
    int duplicateCount = 0;

    entries = calloc(MAX_STUDENTS, sizeof(*entries));
    if (entries == NULL) {
        perror("calloc");
        return 1;
    }

    input = fopen("../../data/student.txt", "r");
    if (input == NULL) {
        perror("student.txt");
        free(entries);
        return 1;
    }

    /* Skip descriptive header. */
    if (fgets(line, sizeof(line), input) == NULL) {
        fprintf(stderr, "Dataset is empty\n");
        fclose(input);
        free(entries);
        return 1;
    }

    PF_Init();

    remove("student_records.dat");
    remove("student_am.0");

    status = SPStore_CreateFile("student_records.dat");
    if (status != SPSTORE_OK) {
        fprintf(stderr, "Create failed: %s\n", SPStore_ErrorString(status));
        fclose(input);
        free(entries);
        return 1;
    }

    status = SPStore_Open(&store, "student_records.dat");
    if (status != SPSTORE_OK) {
        fprintf(stderr, "Open failed: %s\n", SPStore_ErrorString(status));
        fclose(input);
        free(entries);
        return 1;
    }

    /* Load records and save their actual RIDs. */
    while (fgets(line, sizeof(line), input) != NULL) {
        char *first;
        char *second;
        size_t length;
        size_t rollLength;
        int rid;

        length = strlen(line);

        while (length > 0 &&
              (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }

        if (length == 0)
            continue;

        if (count >= MAX_STUDENTS) {
            fprintf(stderr, "Student limit exceeded\n");
            return 1;
        }

        first = strchr(line, ';');
        if (first == NULL) {
            fprintf(stderr, "Invalid record: %s\n", line);
            return 1;
        }

        second = strchr(first + 1, ';');
        if (second == NULL) {
            fprintf(stderr, "Invalid record: %s\n", line);
            return 1;
        }

        rollLength = (size_t)(second - (first + 1));

        if (rollLength != ROLL_LENGTH) {
            fprintf(stderr, "Unexpected roll number: %s\n", first + 1);
            return 1;
        }

        memcpy(entries[count].roll, first + 1, ROLL_LENGTH);

        status = SPStore_Insert(&store, line, (uint16_t)length, &rid);
        if (status != SPSTORE_OK) {
            fprintf(stderr, "Storage insert failed: %s\n",
                    SPStore_ErrorString(status));
            return 1;
        }

        entries[count].rid = rid;
        count++;
    }

    fclose(input);

    printf("Student records loaded: %d\n", count);

    /* Build an index over the existing Student records. */
    xAM_CreateIndex("student_am", 0, CHAR_TYPE, ROLL_LENGTH);

    indexFd = xPF_OpenFile("student_am.0");

    for (i = 0; i < count; i++) {
        xAM_InsertEntry(indexFd, CHAR_TYPE, ROLL_LENGTH,
                        entries[i].roll, entries[i].rid);
    }

    printf("Index entries inserted: %d\n", count);

    /* Search for a duplicate roll number. */
    scanDesc = xAM_OpenIndexScan(indexFd, CHAR_TYPE, ROLL_LENGTH,
                                 EQ_OP, duplicateKey);

    while (1) {
        int rid = xAM_FindNextEntry(scanDesc);

        if (rid == AME_EOF)
            break;

        if (rid < 0) {
            fprintf(stderr, "Unexpected scan error: %d\n", rid);
            return 1;
        }

        printf("Matching RID: %d\n", rid);
        duplicateCount++;
    }

    xAM_CloseIndexScan(scanDesc);
    xPF_CloseFile(indexFd);
    SPStore_Close(&store);

    printf("\n========== AM TEST RESULT ==========\n");
    printf("Total Student records : %d\n", count);
    printf("Search roll number    : %.*s\n", ROLL_LENGTH, duplicateKey);
    printf("Matching records      : %d\n", duplicateCount);

    free(entries);

    if (duplicateCount != 2) {
        fprintf(stderr, "FAIL: Expected 2 matching records\n");
        return 1;
    }

    printf("PASS: One-shot Student index construction\n");
    printf("PASS: Duplicate roll-number search\n");

    return 0;}
