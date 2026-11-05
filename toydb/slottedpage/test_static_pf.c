/*
 * PF-backed static-record integration test.
 *
 * Usage:
 *   ./test_static_pf /path/to/student.txt [max_record_length] [--keep]
 *
 * The test loads the supplied Student dataset, closes/reopens the PF file,
 * checks all records through sequential scan and RID retrieval, and prints
 * storage statistics. By default the temporary PF file is removed. With
 * --keep, a file named static_students_M<max>.pf is left in the current
 * working directory for inspection.
 */
#include "static_store.h"
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

typedef struct VerifyContext {
    SavedStudent *students;
    int count;
    int next;
    int failed;
} VerifyContext;

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

static int verify_scan_callback(int rid, const void *record,
                                uint16_t record_length, void *context)
{
    VerifyContext *verify = (VerifyContext *)context;
    SavedStudent *expected;

    if (verify->next >= verify->count) {
        verify->failed = 1;
        return -1;
    }

    expected = &verify->students[verify->next];
    if (rid != expected->rid || record_length != expected->length ||
        memcmp(record, expected->text, record_length) != 0) {
        verify->failed = 1;
        return -1;
    }

    ++verify->next;
    return SRSTORE_SCAN_CONTINUE;
}

static int parse_max_length(const char *text, uint16_t *out)
{
    char *end = NULL;
    unsigned long value;

    value = strtoul(text, &end, 10);
    if (text[0] == '\0' || end == text || *end != '\0' ||
        value == 0 || value > 65535UL)
        return -1;

    *out = (uint16_t)value;
    return 0;
}

static int fail_test(SRStore *store, const char *path,
                     SavedStudent *students, int count,
                     FILE *input, const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    if (input != NULL)
        fclose(input);
    if (store != NULL && store->fd >= 0)
        (void)SRStore_Close(store);
    if (path != NULL)
        (void)SRStore_DestroyFile(path);
    free_students(students, count);
    return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    FILE *input = NULL;
    char line[MAX_LINE];
    char path[256];
    SavedStudent *students = NULL;
    VerifyContext verify;
    SRStore store;
    SRStoreStats stats;
    uint64_t scanned = 0;
    uint16_t max_length = 128;
    int keep_file = 0;
    int count = 0;
    int status;
    int i;

    if (argc < 2 || argc > 4) {
        fprintf(stderr,
                "Usage: %s /path/to/student.txt [max_record_length] [--keep]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 3) {
        if (strcmp(argv[2], "--keep") == 0) {
            keep_file = 1;
        } else if (parse_max_length(argv[2], &max_length) != 0) {
            fprintf(stderr, "Invalid maximum record length: %s\n", argv[2]);
            return EXIT_FAILURE;
        }
    }
    if (argc == 4) {
        if (strcmp(argv[3], "--keep") != 0) {
            fprintf(stderr, "The fourth argument must be --keep\n");
            return EXIT_FAILURE;
        }
        keep_file = 1;
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
        perror("fopen Student dataset");
        free(students);
        return EXIT_FAILURE;
    }

    /* First line is a descriptive heading, not a Student record. */
    if (fgets(line, sizeof(line), input) == NULL)
        return fail_test(NULL, NULL, students, 0, input, "dataset is empty");

    if (keep_file) {
        (void)snprintf(path, sizeof(path), "static_students_M%u.pf", max_length);
    } else {
        (void)snprintf(path, sizeof(path), "/tmp/toydb_static_M%u_%ld.pf",
                       max_length, (long)getpid());
    }

    /* This test owns this generated path; remove a stale prior run. */
    (void)unlink(path);

    PF_Init();
    status = SRStore_CreateFile(path);
    if (status != SRSTORE_OK)
        return fail_test(NULL, NULL, students, 0, input,
                         SRStore_ErrorString(status));

    status = SRStore_Open(&store, path, max_length);
    if (status != SRSTORE_OK)
        return fail_test(NULL, path, students, 0, input,
                         SRStore_ErrorString(status));

    while (fgets(line, sizeof(line), input) != NULL) {
        size_t n;
        char *copy;

        trim_eol(line);
        n = strlen(line);
        if (n == 0)
            continue;

        if (n > UINT16_MAX || count >= MAX_STUDENTS)
            return fail_test(&store, path, students, count, input,
                             "record length/count exceeded test limits");
        if (n > max_length) {
            fprintf(stderr,
                    "Dataset record %d has %lu bytes, greater than M=%u\n",
                    count, (unsigned long)n, max_length);
            return fail_test(&store, path, students, count, input,
                             "maximum record length is too small for the dataset");
        }

        copy = (char *)malloc(n + 1);
        if (copy == NULL) {
            perror("malloc record");
            return fail_test(&store, path, students, count, input,
                             "record allocation failed");
        }
        memcpy(copy, line, n + 1);

        status = SRStore_Insert(&store, copy, (uint16_t)n,
                                &students[count].rid);
        if (status != SRSTORE_OK) {
            fprintf(stderr, "Insert failed at record %d: %s (PF=%d)\n",
                    count, SRStore_ErrorString(status), store.last_pf_error);
            free(copy);
            return fail_test(&store, path, students, count, input,
                             "static PF insertion failed");
        }

        students[count].text = copy;
        students[count].length = (uint16_t)n;
        ++count;
    }
    fclose(input);
    input = NULL;

    status = SRStore_GetStats(&store, &stats);
    if (status != SRSTORE_OK)
        return fail_test(&store, path, students, count, NULL,
                         "could not collect pre-close storage statistics");

    if (SRStore_Close(&store) != SRSTORE_OK)
        return fail_test(&store, path, students, count, NULL,
                         "close before persistence verification failed");

    /* Reopen the PF file to prove records survived the close. */
    status = SRStore_Open(&store, path, max_length);
    if (status != SRSTORE_OK)
        return fail_test(NULL, path, students, count, NULL,
                         "reopen after insertion failed");

    verify.students = students;
    verify.count = count;
    verify.next = 0;
    verify.failed = 0;

    status = SRStore_Scan(&store, verify_scan_callback, &verify, &scanned);
    if (status != SRSTORE_OK || verify.failed || verify.next != count ||
        scanned != (uint64_t)count)
        return fail_test(&store, path, students, count, NULL,
                         "sequential scan did not match all inserted records");

    /* Also validate direct page/slot retrieval using every returned RID. */
    for (i = 0; i < count; ++i) {
        unsigned char output[SR_PAGE_SIZE];
        uint16_t output_length = 0;

        status = SRStore_Get(&store, students[i].rid, output,
                             (uint16_t)sizeof(output), &output_length);
        if (status != SR_OK || output_length != students[i].length ||
            memcmp(output, students[i].text, output_length) != 0)
            return fail_test(&store, path, students, count, NULL,
                             "RID-based retrieval verification failed");
    }

    status = SRStore_GetStats(&store, &stats);
    if (status != SRSTORE_OK)
        return fail_test(&store, path, students, count, NULL,
                         "could not collect final storage statistics");

    if (SRStore_Close(&store) != SRSTORE_OK)
        return fail_test(NULL, path, students, count, NULL,
                         "final PF close failed");

    printf("PASS: PF-backed static-record Student dataset test\n");
    printf("Maximum record length (M) : %u bytes\n", max_length);
    printf("Student records inserted  : %d\n", count);
    printf("Student records scanned   : %llu\n", (unsigned long long)scanned);
    printf("RID retrievals verified   : %d\n", count);
    printf("PF pages allocated       : %llu\n", (unsigned long long)stats.page_count);
    printf("Actual payload bytes     : %llu\n", (unsigned long long)stats.payload_bytes);
    printf("Reserved record bytes    : %llu\n", (unsigned long long)stats.record_slot_bytes);
    printf("Length metadata bytes    : %llu\n", (unsigned long long)stats.length_metadata_bytes);
    printf("Page header bytes        : %llu\n", (unsigned long long)stats.page_header_bytes);
    printf("Occupied page bytes     : %llu\n", (unsigned long long)stats.occupied_bytes);
    printf("Allocated page bytes    : %llu\n", (unsigned long long)stats.allocated_page_bytes);
    printf("Reserved padding bytes  : %llu\n", (unsigned long long)stats.reserved_padding_bytes);
    printf("Unused page bytes       : %llu\n", (unsigned long long)stats.unused_bytes);
    printf("PF page-link bytes      : %llu\n", (unsigned long long)stats.pf_page_link_bytes);
    printf("PF file-header bytes    : %llu\n", (unsigned long long)stats.pf_file_header_bytes);
    printf("Estimated PF file size  : %llu\n", (unsigned long long)stats.physical_file_bytes);

    if (keep_file) {
        printf("PF database retained    : %s\n", path);
    } else {
        if (SRStore_DestroyFile(path) != SRSTORE_OK) {
            fprintf(stderr, "WARNING: could not remove temporary PF file %s\n", path);
            free_students(students, count);
            return EXIT_FAILURE;
        }
        printf("Temporary PF database removed after verification.\n");
    }

    free_students(students, count);
    return EXIT_SUCCESS;
}
