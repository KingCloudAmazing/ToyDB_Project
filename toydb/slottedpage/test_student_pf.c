/*
 * End-to-end PF-backed test using the supplied student.txt dataset.
 * The first descriptive line is skipped. Every inserted row is read back
 * using its RID after closing and reopening the PF file.
 *
 * Usage: ./test_student_pf /path/to/student.txt
 */
#include "student_store.h"
#include "slotted_page.h"

#include "pf_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_STUDENTS 30000
#define MAX_LINE 4096

struct SavedStudent {
    char *text;
    uint16_t length;
    int rid;
};

static void trim_eol(char *text)
{
    size_t n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r'))
        text[--n] = '\0';
}

/*
 * Count the actual allocated PF pages in an open PF file.
 * Each page is released without marking it dirty before advancing.
 */
static int count_allocated_pf_pages(int fd, int *count_out)
{
    int page_num = -1;
    int pf_status;
    int count = 0;
    char *page_buf = NULL;

    if (count_out == NULL)
        return -1;

    *count_out = 0;

    pf_status = PF_GetFirstPage(fd, &page_num, &page_buf);
    if (pf_status == PFE_EOF)
        return PFE_OK;
    if (pf_status != PFE_OK)
        return pf_status;

    for (;;) {
        int next_page;

        ++count;

        pf_status = PF_UnfixPage(fd, page_num, FALSE);
        if (pf_status != PFE_OK)
            return pf_status;

        next_page = page_num;
        pf_status = PF_GetNextPage(fd, &next_page, &page_buf);
        if (pf_status == PFE_EOF)
            break;
        if (pf_status != PFE_OK)
            return pf_status;

        page_num = next_page;
    }

    *count_out = count;
    return PFE_OK;
}

static void cleanup(struct SavedStudent *students, int count)
{
    int i;
    for (i = 0; i < count; ++i)
        free(students[i].text);
    free(students);
}

int main(int argc, char **argv)
{
    FILE *input;
    char line[MAX_LINE];
    char path[256];
    struct SavedStudent *students;
    SPStore store;
    int count = 0;
    int i;
    int status;
    int pf_page_count = 0;
    int pf_count_status;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s /path/to/student.txt\n", argv[0]);
        return EXIT_FAILURE;
    }

    students = (struct SavedStudent *)calloc(MAX_STUDENTS, sizeof(*students));
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

    if (fgets(line, sizeof(line), input) == NULL) {
        fprintf(stderr, "Dataset is empty\n");
        fclose(input);
        free(students);
        return EXIT_FAILURE;
    }

    (void)snprintf(path, sizeof(path), "/tmp/toydb_student_pf_%ld.dat", (long)getpid());
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
            cleanup(students, count);
            (void)SPStore_DestroyFile(path);
            return EXIT_FAILURE;
        }

        copy = (char *)malloc(n + 1);
        if (copy == NULL) {
            perror("malloc record");
            fclose(input);
            (void)SPStore_Close(&store);
            cleanup(students, count);
            (void)SPStore_DestroyFile(path);
            return EXIT_FAILURE;
        }
        memcpy(copy, line, n + 1);

        status = SPStore_Insert(&store, copy, (uint16_t)n, &students[count].rid);
        if (status != SPSTORE_OK) {
            fprintf(stderr, "Insert failed at student %d: %s (PF=%d)\n", count,
                    SPStore_ErrorString(status), store.last_pf_error);
            free(copy);
            fclose(input);
            (void)SPStore_Close(&store);
            cleanup(students, count);
            (void)SPStore_DestroyFile(path);
            return EXIT_FAILURE;
        }

        students[count].text = copy;
        students[count].length = (uint16_t)n;
        ++count;
    }
    fclose(input);

    status = SPStore_Close(&store);
    if (status != SPSTORE_OK) {
        fprintf(stderr, "Close failed: %s (PF=%d)\n",
                SPStore_ErrorString(status), store.last_pf_error);
        cleanup(students, count);
        (void)SPStore_DestroyFile(path);
        return EXIT_FAILURE;
    }

    status = SPStore_Open(&store, path);
    if (status != SPSTORE_OK) {
        fprintf(stderr, "Reopen failed: %s (PF=%d)\n",
                SPStore_ErrorString(status), store.last_pf_error);
        cleanup(students, count);
        (void)SPStore_DestroyFile(path);
        return EXIT_FAILURE;
    }

    for (i = 0; i < count; ++i) {
        char output[MAX_LINE];
        uint16_t output_length = 0;

        status = SPStore_Get(&store, students[i].rid, output,
                             sizeof(output), &output_length);
        if (status != SP_OK || output_length != students[i].length ||
            memcmp(output, students[i].text, students[i].length) != 0) {
            fprintf(stderr, "Persistent verification failed at record %d (status=%d)\n",
                    i, status);
            (void)SPStore_Close(&store);
            cleanup(students, count);
            (void)SPStore_DestroyFile(path);
            return EXIT_FAILURE;
        }
    }

    pf_count_status = count_allocated_pf_pages(store.fd, &pf_page_count);
    if (pf_count_status != PFE_OK) {
        fprintf(stderr, "PF page-count traversal failed (PF=%d)\n",
                pf_count_status);
        (void)SPStore_Close(&store);
        cleanup(students, count);
        (void)SPStore_DestroyFile(path);
        return EXIT_FAILURE;
    }

    status = SPStore_Close(&store);
    if (status != SPSTORE_OK) {
        fprintf(stderr, "Final close failed\n");
        cleanup(students, count);
        (void)SPStore_DestroyFile(path);
        return EXIT_FAILURE;
    }

    if (SPStore_DestroyFile(path) != SPSTORE_OK) {
        fprintf(stderr, "Could not remove temporary PF file\n");
        cleanup(students, count);
        return EXIT_FAILURE;
    }

    printf("PASS: PF-backed Student dataset insert/close/reopen/retrieve\n");
    printf("Student records verified : %d\n", count);
    printf("PF pages allocated       : %d\n", pf_page_count);
    cleanup(students, count);
    return EXIT_SUCCESS;
}
