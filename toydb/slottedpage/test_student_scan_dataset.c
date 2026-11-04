/*
 * Full supplied Student dataset scan test.
 * Loads student.txt into PF-backed slotted pages, closes/reopens the file,
 * and verifies every callback record against the original input order.
 * Usage: ./test_student_scan_dataset /path/to/student.txt
 */
#include "student_store.h"
#include "pf_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_STUDENTS 30000
#define MAX_LINE 4096

typedef struct SavedStudent {
    char *text;
    uint16_t length;
    int rid;
} SavedStudent;

typedef struct DatasetScanContext {
    SavedStudent *students;
    int count;
    int next;
    int failed;
} DatasetScanContext;

static void trim_eol(char *text)
{
    size_t n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r'))
        text[--n] = '\0';
}

static void free_students(SavedStudent *students, int count)
{
    int i;
    for (i = 0; i < count; ++i)
        free(students[i].text);
    free(students);
}

static int verify_dataset_callback(int rid, const void *record,
                                   uint16_t record_length, void *context)
{
    DatasetScanContext *scan = (DatasetScanContext *)context;
    SavedStudent *expected;

    if (scan->next >= scan->count) {
        scan->failed = 1;
        return -1;
    }

    expected = &scan->students[scan->next];
    if (rid != expected->rid || record_length != expected->length ||
        memcmp(record, expected->text, record_length) != 0) {
        scan->failed = 1;
        return -1;
    }

    ++scan->next;
    return SPSTORE_SCAN_CONTINUE;
}

int main(int argc, char **argv)
{
    FILE *input;
    char line[MAX_LINE];
    char path[256];
    SavedStudent *students;
    DatasetScanContext scan;
    SPStore store;
    uint64_t scanned = 0;
    int count = 0;
    int status;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s /path/to/student.txt\n", argv[0]);
        return EXIT_FAILURE;
    }

    memset(&store, 0, sizeof(store));
    store.fd = -1;
    students = (SavedStudent *)calloc(MAX_STUDENTS, sizeof(*students));
    if (students == NULL) {
        perror("calloc students");
        return EXIT_FAILURE;
    }

    input = fopen(argv[1], "r");
    if (input == NULL) {
        perror("fopen student dataset");
        free(students);
        return EXIT_FAILURE;
    }

    /* The supplied file begins with a descriptive, non-record line. */
    if (fgets(line, sizeof(line), input) == NULL) {
        fprintf(stderr, "Dataset is empty\n");
        fclose(input);
        free(students);
        return EXIT_FAILURE;
    }

    (void)snprintf(path, sizeof(path), "/tmp/toydb_student_scan_%ld.pf", (long)getpid());
    PF_Init();

    status = SPStore_CreateFile(path);
    if (status != SPSTORE_OK) {
        fprintf(stderr, "Create failed: %s\n", SPStore_ErrorString(status));
        fclose(input);
        free(students);
        return EXIT_FAILURE;
    }

    status = SPStore_Open(&store, path);
    if (status != SPSTORE_OK) {
        fprintf(stderr, "Open failed: %s (PF=%d)\n",
                SPStore_ErrorString(status), store.last_pf_error);
        fclose(input);
        free(students);
        (void)SPStore_DestroyFile(path);
        return EXIT_FAILURE;
    }

    while (fgets(line, sizeof(line), input) != NULL) {
        size_t n;
        char *copy;

        trim_eol(line);
        n = strlen(line);
        if (n == 0)
            continue;
        if (n > UINT16_MAX || count >= MAX_STUDENTS) {
            fprintf(stderr, "Unexpected record length/count limit\n");
            fclose(input);
            (void)SPStore_Close(&store);
            free_students(students, count);
            (void)SPStore_DestroyFile(path);
            return EXIT_FAILURE;
        }

        copy = (char *)malloc(n + 1);
        if (copy == NULL) {
            perror("malloc record");
            fclose(input);
            (void)SPStore_Close(&store);
            free_students(students, count);
            (void)SPStore_DestroyFile(path);
            return EXIT_FAILURE;
        }
        memcpy(copy, line, n + 1);

        status = SPStore_Insert(&store, copy, (uint16_t)n, &students[count].rid);
        if (status != SPSTORE_OK) {
            fprintf(stderr, "Insert failed at row %d: %s (PF=%d)\n",
                    count, SPStore_ErrorString(status), store.last_pf_error);
            free(copy);
            fclose(input);
            (void)SPStore_Close(&store);
            free_students(students, count);
            (void)SPStore_DestroyFile(path);
            return EXIT_FAILURE;
        }

        students[count].text = copy;
        students[count].length = (uint16_t)n;
        ++count;
    }
    fclose(input);

    if (SPStore_Close(&store) != SPSTORE_OK) {
        fprintf(stderr, "Close before scan failed\n");
        free_students(students, count);
        (void)SPStore_DestroyFile(path);
        return EXIT_FAILURE;
    }

    if (SPStore_Open(&store, path) != SPSTORE_OK) {
        fprintf(stderr, "Reopen before scan failed\n");
        free_students(students, count);
        (void)SPStore_DestroyFile(path);
        return EXIT_FAILURE;
    }

    scan.students = students;
    scan.count = count;
    scan.next = 0;
    scan.failed = 0;

    status = SPStore_Scan(&store, verify_dataset_callback, &scan, &scanned);
    if (status != SPSTORE_OK || scan.failed || scan.next != count || scanned != (uint64_t)count) {
        fprintf(stderr,
                "Scan verification failed: status=%d, callback rows=%d, expected=%d, scanned=%llu\n",
                status, scan.next, count, (unsigned long long)scanned);
        (void)SPStore_Close(&store);
        free_students(students, count);
        (void)SPStore_DestroyFile(path);
        return EXIT_FAILURE;
    }

    if (SPStore_Close(&store) != SPSTORE_OK) {
        fprintf(stderr, "Final close failed\n");
        free_students(students, count);
        (void)SPStore_DestroyFile(path);
        return EXIT_FAILURE;
    }

    if (SPStore_DestroyFile(path) != SPSTORE_OK) {
        fprintf(stderr, "Destroy failed\n");
        free_students(students, count);
        return EXIT_FAILURE;
    }

    printf("PASS: PF-backed sequential scan of the supplied Student dataset\n");
    printf("Student records inserted : %d\n", count);
    printf("Student records scanned  : %llu\n", (unsigned long long)scanned);
    printf("Missing/extra records    : 0\n");
    free_students(students, count);
    return EXIT_SUCCESS;
}
