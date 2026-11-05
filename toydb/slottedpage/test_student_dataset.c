/*
 * Stress-test the page-level slotted-page implementation using student.txt.
 * This is an in-memory page-packing test; it does not yet exercise PF I/O.
 *
 * Usage:
 *   ./test_student_dataset /path/to/student.txt
 */

#include "slotted_page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STUDENT_RECORDS 30000
#define MAX_STUDENT_PAGES   2000
#define INPUT_LINE_SIZE     4096

typedef struct StudentLocation {
    int page_index;
    uint16_t slot_num;
    uint16_t length;
} StudentLocation;

static void trim_line_end(char *line)
{
    size_t length = strlen(line);

    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[--length] = '\0';
    }
}

int main(int argc, char **argv)
{
    FILE *input;
    char line[INPUT_LINE_SIZE];
    char *pages[MAX_STUDENT_PAGES];
    StudentLocation *locations;
    int page_count = 0;
    int record_count = 0;
    int current_page = -1;
    int i;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s /path/to/student.txt\n", argv[0]);
        return EXIT_FAILURE;
    }

    memset(pages, 0, sizeof(pages));
    locations = (StudentLocation *)calloc(MAX_STUDENT_RECORDS, sizeof(*locations));
    if (locations == NULL) {
        perror("calloc locations");
        return EXIT_FAILURE;
    }

    input = fopen(argv[1], "r");
    if (input == NULL) {
        perror("fopen student dataset");
        free(locations);
        return EXIT_FAILURE;
    }

    /* The first line is the dataset description, not a student record. */
    if (fgets(line, sizeof(line), input) == NULL) {
        fprintf(stderr, "Dataset is empty\n");
        fclose(input);
        free(locations);
        return EXIT_FAILURE;
    }

    while (fgets(line, sizeof(line), input) != NULL) {
        size_t length;
        uint16_t slot_num;
        int status;

        trim_line_end(line);
        length = strlen(line);

        /* Ignore the blank trailing line, if present. */
        if (length == 0)
            continue;

        if (length > UINT16_MAX || record_count >= MAX_STUDENT_RECORDS) {
            fprintf(stderr, "Record size/count exceeds test limits\n");
            fclose(input);
            free(locations);
            return EXIT_FAILURE;
        }

        if (current_page < 0) {
            current_page = page_count;
            if (page_count >= MAX_STUDENT_PAGES) {
                fprintf(stderr, "Exceeded test page limit\n");
                fclose(input);
                free(locations);
                return EXIT_FAILURE;
            }
            pages[page_count] = (char *)malloc(SP_PAGE_SIZE);
            if (pages[page_count] == NULL || SP_InitPage(pages[page_count]) != SP_OK) {
                fprintf(stderr, "Could not allocate/init page %d\n", page_count);
                fclose(input);
                free(locations);
                return EXIT_FAILURE;
            }
            ++page_count;
        }

        status = SP_InsertRecord(pages[current_page], line, (uint16_t)length, &slot_num);
        if (status == SP_ERR_PAGE_FULL) {
            current_page = page_count;
            if (page_count >= MAX_STUDENT_PAGES) {
                fprintf(stderr, "Exceeded test page limit\n");
                fclose(input);
                free(locations);
                return EXIT_FAILURE;
            }
            pages[page_count] = (char *)malloc(SP_PAGE_SIZE);
            if (pages[page_count] == NULL || SP_InitPage(pages[page_count]) != SP_OK) {
                fprintf(stderr, "Could not allocate/init page %d\n", page_count);
                fclose(input);
                free(locations);
                return EXIT_FAILURE;
            }
            ++page_count;
            status = SP_InsertRecord(pages[current_page], line, (uint16_t)length, &slot_num);
        }

        if (status != SP_OK) {
            fprintf(stderr, "Insert failed at record %d: %s\n",
                    record_count, SP_ErrorString(status));
            fclose(input);
            free(locations);
            return EXIT_FAILURE;
        }

        locations[record_count].page_index = current_page;
        locations[record_count].slot_num = slot_num;
        locations[record_count].length = (uint16_t)length;
        ++record_count;
    }

    fclose(input);

    /* Re-read the source file and compare each original row with its stored bytes. */
    input = fopen(argv[1], "r");
    if (input == NULL) {
        perror("reopen student dataset");
        free(locations);
        return EXIT_FAILURE;
    }

    if (fgets(line, sizeof(line), input) == NULL) {
        fprintf(stderr, "Could not reread dataset header\n");
        fclose(input);
        free(locations);
        return EXIT_FAILURE;
    }

    i = 0;
    while (fgets(line, sizeof(line), input) != NULL) {
        char output[SP_PAGE_SIZE];
        uint16_t output_length = 0;
        size_t original_length;
        int status;

        trim_line_end(line);
        original_length = strlen(line);
        if (original_length == 0)
            continue;

        if (i >= record_count) {
            fprintf(stderr, "Second pass found more records than first pass\n");
            fclose(input);
            free(locations);
            return EXIT_FAILURE;
        }

        status = SP_GetRecord(pages[locations[i].page_index],
                              locations[i].slot_num,
                              output,
                              sizeof(output),
                              &output_length);
        if (status != SP_OK || output_length != original_length ||
            memcmp(output, line, original_length) != 0) {
            fprintf(stderr, "Round-trip verification failed at record %d\n", i);
            fclose(input);
            free(locations);
            return EXIT_FAILURE;
        }
        ++i;
    }

    fclose(input);

    if (i != record_count) {
        fprintf(stderr, "Record count mismatch: first pass=%d, second pass=%d\n",
                record_count, i);
        free(locations);
        return EXIT_FAILURE;
    }

    printf("PASS: Student dataset in-memory slotted-page round trip\n");
    printf("      Student records verified : %d\n", record_count);
    printf("      Slotted pages used       : %d\n", page_count);
    printf("      Average records per page : %.2f\n",
           page_count ? (double)record_count / (double)page_count : 0.0);

    for (i = 0; i < page_count; ++i)
        free(pages[i]);
    free(locations);

    return EXIT_SUCCESS;
}
