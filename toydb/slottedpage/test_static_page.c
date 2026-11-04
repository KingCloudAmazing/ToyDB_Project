#include "static_page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_MAX_RECORD_LENGTH 256

static int test_one_max_length(uint16_t max_length)
{
    unsigned char *page;
    unsigned char record[TEST_MAX_RECORD_LENGTH];
    unsigned char output[TEST_MAX_RECORD_LENGTH];

    uint16_t capacity;
    uint16_t count;
    uint16_t slot;
    uint16_t retrieved_length;
    uint16_t i;

    uint32_t used_bytes;
    uint32_t payload_bytes;
    uint32_t expected_payload = 0;

    int status;

    page = (unsigned char *)malloc(SR_PAGE_SIZE);
    if (page == NULL) {
        perror("malloc");
        return 0;
    }

    status = SR_InitPage(page, max_length);
    if (status != SR_OK) {
        printf("FAIL: page initialization for M=%u\n", max_length);
        free(page);
        return 0;
    }

    if (!SR_IsValidPage(page)) {
        printf("FAIL: page validation for M=%u\n", max_length);
        free(page);
        return 0;
    }

    status = SR_GetCapacity(page, &capacity);
    if (status != SR_OK || capacity == 0) {
        printf("FAIL: capacity calculation for M=%u\n", max_length);
        free(page);
        return 0;
    }

    /*
     * Fill every available slot with variable-length records.
     * All records are at most 108 bytes, matching the supplied dataset.
     */
    for (i = 0; i < capacity; ++i) {
        uint16_t length = (uint16_t)(91 + (i % 18));
        uint16_t j;

        for (j = 0; j < length; ++j)
            record[j] = (unsigned char)('A' + (i % 26));

        /* Make each record distinguishable. */
        record[0] = (unsigned char)i;

        status = SR_InsertRecord(page, record, length, &slot);

        if (status != SR_OK || slot != i) {
            printf("FAIL: insertion at slot %u for M=%u\n",
                   i, max_length);
            free(page);
            return 0;
        }

        memset(output, 0, sizeof(output));

        status = SR_GetRecord(page, slot, output,
                              sizeof(output), &retrieved_length);

        if (status != SR_OK ||
            retrieved_length != length ||
            memcmp(record, output, length) != 0) {
            printf("FAIL: retrieval at slot %u for M=%u\n",
                   i, max_length);
            free(page);
            return 0;
        }

        expected_payload += length;
    }

    status = SR_GetRecordCount(page, &count);
    if (status != SR_OK || count != capacity) {
        printf("FAIL: record count for M=%u\n", max_length);
        free(page);
        return 0;
    }

    status = SR_GetUsedBytes(page, &used_bytes);
    if (status != SR_OK) {
        printf("FAIL: used-byte calculation for M=%u\n", max_length);
        free(page);
        return 0;
    }

    status = SR_GetPayloadBytes(page, &payload_bytes);
    if (status != SR_OK || payload_bytes != expected_payload) {
        printf("FAIL: payload-byte calculation for M=%u\n", max_length);
        free(page);
        return 0;
    }

    /* An additional record must not fit. */
    status = SR_InsertRecord(page, record, 91, &slot);

    if (status != SR_ERR_PAGE_FULL) {
        printf("FAIL: full-page detection for M=%u (status=%d)\n",
               max_length, status);
        free(page);
        return 0;
    }

    printf("PASS: M=%u, capacity=%u, used=%u bytes, payload=%u bytes\n",
           max_length, capacity, used_bytes, payload_bytes);

    free(page);
    return 1;
}

int main(void)
{
    static const uint16_t max_lengths[] = {
        108, 112, 128, 160, 256
    };

    size_t i;

    printf("===== STATIC RECORD PAGE TESTS =====\n");

    for (i = 0; i < sizeof(max_lengths) / sizeof(max_lengths[0]); ++i) {
        if (!test_one_max_length(max_lengths[i]))
            return EXIT_FAILURE;
    }

    printf("\nAll static-record page tests passed.\n");

    return EXIT_SUCCESS;
}