#include "slotted_page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
            return EXIT_FAILURE; \
        } \
    } while (0)

static int test_init_and_round_trip(void)
{
    char page[SP_PAGE_SIZE];
    const char *records[] = {
        "949981;95302001;XXXXXXXXX;M;XXXXXXXXX;XXXXXXXXX;XXXXXXXXX;XXXXXXXXX;XXXXXXXXX;XXXXXXXXX;",
        "949982;95310001;XXXXXXXXX;M;XXXXXXXXX;XXXXXXXXX;XXXXXXXXX;XXXXXXXXX;XXXXXXXXX;",
        "949983;95305001;XXXXXXXXX;M;XXXXXXXXX;XXXXXXXXX;XXXXXXXXX;XXXXXXXXX;XXXXXXXXX;XXXXXXXXX;XXXXXXXXX;"
    };
    uint16_t slots[3];
    size_t lengths[3];
    size_t i;
    uint16_t slot_count;
    uint16_t live_count;
    uint16_t free_space;

    CHECK(SP_InitPage(page) == SP_OK, "initialize page");
    CHECK(SP_IsValidPage(page), "initialized page validates");
    CHECK(SP_GetSlotCount(page, &slot_count) == SP_OK && slot_count == 0,
          "empty page has zero slots");
    CHECK(SP_GetLiveCount(page, &live_count) == SP_OK && live_count == 0,
          "empty page has zero live records");

    for (i = 0; i < 3; ++i) {
        lengths[i] = strlen(records[i]);
        CHECK(SP_InsertRecord(page, records[i], (uint16_t)lengths[i], &slots[i]) == SP_OK,
              "insert sample record");
        CHECK(slots[i] == i, "new slots are assigned sequentially");
    }

    CHECK(SP_GetSlotCount(page, &slot_count) == SP_OK && slot_count == 3,
          "slot count reflects insertions");
    CHECK(SP_GetLiveCount(page, &live_count) == SP_OK && live_count == 3,
          "live count reflects insertions");

    for (i = 0; i < 3; ++i) {
        char output[SP_PAGE_SIZE];
        uint16_t output_length = 0;

        CHECK(SP_GetRecord(page, slots[i], output, sizeof(output), &output_length) == SP_OK,
              "retrieve inserted record");
        CHECK(output_length == lengths[i], "retrieved record length matches");
        CHECK(memcmp(output, records[i], lengths[i]) == 0,
              "retrieved bytes exactly match original record");
    }

    CHECK(SP_GetFreeSpace(page, &free_space) == SP_OK,
          "query free space");
    CHECK(free_space < SP_PAGE_SIZE - sizeof(SPHeader),
          "free space decreases after insertion");

    printf("PASS: initialization, metadata, variable-length insertion, and retrieval\n");
    printf("      records=%u, free_space=%u bytes\n", (unsigned)live_count,
           (unsigned)free_space);
    return EXIT_SUCCESS;
}

static int test_invalid_inputs_and_page_full(void)
{
    char page[SP_PAGE_SIZE];
    char record[SP_PAGE_SIZE];
    char output[SP_PAGE_SIZE];
    uint16_t slot;
    uint16_t output_length;
    int status;
    unsigned int successful_inserts = 0;

    memset(record, 'R', sizeof(record));
    CHECK(SP_InitPage(page) == SP_OK, "initialize page for capacity test");

    CHECK(SP_InsertRecord(page, record, 0, &slot) == SP_ERR_INVALID_ARGUMENT,
          "zero-length record rejected");
    CHECK(SP_InsertRecord(page, record, 1, NULL) == SP_ERR_INVALID_ARGUMENT,
          "NULL slot output rejected");

    /* Insert records until the page reports that it is full. */
    for (;;) {
        status = SP_InsertRecord(page, record, 100, &slot);
        if (status == SP_ERR_PAGE_FULL)
            break;
        CHECK(status == SP_OK, "capacity test insertion succeeds or reports full");
        ++successful_inserts;
    }

    CHECK(successful_inserts > 0, "at least one record fits");
    CHECK(SP_IsValidPage(page), "page remains valid after filling");

    /* Check that all records inserted before the full condition are readable. */
    {
        unsigned int i;
        for (i = 0; i < successful_inserts; ++i) {
            CHECK(SP_GetRecord(page, (uint16_t)i, output, sizeof(output),
                               &output_length) == SP_OK,
                  "all records on full page remain readable");
            CHECK(output_length == 100, "capacity-test record length preserved");
            CHECK(memcmp(output, record, 100) == 0,
                  "capacity-test record contents preserved");
        }
    }

    CHECK(SP_GetRecord(page, (uint16_t)successful_inserts, output,
                       sizeof(output), &output_length) == SP_ERR_INVALID_SLOT,
          "out-of-range slot is rejected");
    CHECK(SP_GetRecord(page, 0, output, 1, &output_length) == SP_ERR_BUFFER_TOO_SMALL,
          "undersized output buffer is rejected");

    printf("PASS: invalid inputs, page-full handling, and full-page retrieval\n");
    printf("      100-byte records fit per page: %u\n", successful_inserts);
    return EXIT_SUCCESS;
}

int main(void)
{
    if (sizeof(SPHeader) != 12 || sizeof(SPSlot) != 4) {
        fprintf(stderr, "FAIL: unexpected on-page structure sizes\n");
        fprintf(stderr, "      sizeof(SPHeader)=%lu, sizeof(SPSlot)=%lu\n",
                (unsigned long)sizeof(SPHeader), (unsigned long)sizeof(SPSlot));
        return EXIT_FAILURE;
    }

    if (test_init_and_round_trip() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (test_invalid_inputs_and_page_full() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    printf("\nALL SLOTTED-PAGE PHASE 1 TESTS PASSED\n");
    return EXIT_SUCCESS;
}
