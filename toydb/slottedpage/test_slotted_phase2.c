#include "slotted_page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, message) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
        return 0; \
    } \
} while (0)

static void fill_bytes(unsigned char *buffer, size_t length, unsigned char seed)
{
    size_t i;
    for (i = 0; i < length; ++i)
        buffer[i] = (unsigned char)(seed + (unsigned char)(i % 97U));
}

static int get_counts(const char *page, uint16_t expected_slots, uint16_t expected_live)
{
    uint16_t slots = 0;
    uint16_t live = 0;

    if (SP_GetSlotCount(page, &slots) != SP_OK)
        return 0;
    if (SP_GetLiveCount(page, &live) != SP_OK)
        return 0;

    return slots == expected_slots && live == expected_live;
}

static int check_record(const char *page, uint16_t slot,
                        const unsigned char *expected, uint16_t expected_length)
{
    unsigned char output[SP_PAGE_SIZE];
    uint16_t actual_length = 0;
    int status;

    memset(output, 0, sizeof(output));
    status = SP_GetRecord(page, slot, output, sizeof(output), &actual_length);
    return status == SP_OK && actual_length == expected_length &&
           memcmp(output, expected, expected_length) == 0;
}

static int test_delete_and_counts(void)
{
    char page[SP_PAGE_SIZE];
    unsigned char a[100], b[300], c[200];
    uint16_t sa, sb, sc;
    uint16_t length = 0;
    unsigned char output[SP_PAGE_SIZE];

    fill_bytes(a, sizeof(a), 1);
    fill_bytes(b, sizeof(b), 2);
    fill_bytes(c, sizeof(c), 3);

    CHECK(SP_InitPage(page) == SP_OK, "initialize page");
    CHECK(SP_InsertRecord(page, a, sizeof(a), &sa) == SP_OK, "insert A");
    CHECK(SP_InsertRecord(page, b, sizeof(b), &sb) == SP_OK, "insert B");
    CHECK(SP_InsertRecord(page, c, sizeof(c), &sc) == SP_OK, "insert C");
    CHECK(sa == 0 && sb == 1 && sc == 2, "initial slot numbers");
    CHECK(get_counts(page, 3, 3), "initial slot/live counts");

    CHECK(SP_DeleteRecord(page, sb) == SP_OK, "delete B");
    CHECK(get_counts(page, 3, 2), "delete changes live count but not slot count");
    CHECK(SP_GetRecord(page, sb, output, sizeof(output), &length) == SP_ERR_INVALID_SLOT,
          "deleted record cannot be retrieved");
    CHECK(SP_DeleteRecord(page, sb) == SP_ERR_INVALID_SLOT, "double delete rejected");
    CHECK(check_record(page, sa, a, sizeof(a)), "A survives deletion");
    CHECK(check_record(page, sc, c, sizeof(c)), "C survives deletion");

    return 1;
}

static int test_deleted_slot_reuse_without_compaction(void)
{
    char page[SP_PAGE_SIZE];
    unsigned char a[100], b[100], c[100], replacement[80];
    uint16_t sa, sb, sc, sr;
    uint16_t before_free, after_free;

    fill_bytes(a, sizeof(a), 10);
    fill_bytes(b, sizeof(b), 20);
    fill_bytes(c, sizeof(c), 30);
    fill_bytes(replacement, sizeof(replacement), 40);

    CHECK(SP_InitPage(page) == SP_OK, "initialize reuse page");
    CHECK(SP_InsertRecord(page, a, sizeof(a), &sa) == SP_OK, "insert A");
    CHECK(SP_InsertRecord(page, b, sizeof(b), &sb) == SP_OK, "insert B");
    CHECK(SP_InsertRecord(page, c, sizeof(c), &sc) == SP_OK, "insert C");
    CHECK(SP_GetFreeSpace(page, &before_free) == SP_OK, "get free space before deletion");
    CHECK(SP_DeleteRecord(page, sb) == SP_OK, "delete B");

    CHECK(SP_InsertRecord(page, replacement, sizeof(replacement), &sr) == SP_OK,
          "insert replacement into available contiguous space");
    CHECK(sr == sb, "replacement reuses deleted slot number");
    CHECK(get_counts(page, 3, 3), "slot count unchanged and live count restored");
    CHECK(SP_GetFreeSpace(page, &after_free) == SP_OK, "get free space after reuse");
    CHECK(after_free == (uint16_t)(before_free - sizeof(replacement)),
          "contiguous free-space accounting after reuse");
    CHECK(check_record(page, sa, a, sizeof(a)), "A remains retrievable");
    CHECK(check_record(page, sc, c, sizeof(c)), "C remains retrievable");
    CHECK(check_record(page, sr, replacement, sizeof(replacement)), "replacement retrievable");
    return 1;
}

static int test_explicit_compaction_preserves_slots(void)
{
    char page[SP_PAGE_SIZE];
    unsigned char a[500], b[700], c[600], d[400];
    uint16_t sa, sb, sc, sd;
    uint16_t free_before, free_after;

    fill_bytes(a, sizeof(a), 11);
    fill_bytes(b, sizeof(b), 22);
    fill_bytes(c, sizeof(c), 33);
    fill_bytes(d, sizeof(d), 44);

    CHECK(SP_InitPage(page) == SP_OK, "initialize compaction page");
    CHECK(SP_InsertRecord(page, a, sizeof(a), &sa) == SP_OK, "insert A");
    CHECK(SP_InsertRecord(page, b, sizeof(b), &sb) == SP_OK, "insert B");
    CHECK(SP_InsertRecord(page, c, sizeof(c), &sc) == SP_OK, "insert C");
    CHECK(SP_InsertRecord(page, d, sizeof(d), &sd) == SP_OK, "insert D");
    CHECK(SP_DeleteRecord(page, sb) == SP_OK, "delete B");
    CHECK(SP_DeleteRecord(page, sd) == SP_OK, "delete D");
    CHECK(SP_GetFreeSpace(page, &free_before) == SP_OK, "free space before compaction");

    CHECK(SP_CompactPage(page) == SP_OK, "explicit compaction succeeds");
    CHECK(SP_IsValidPage(page), "compacted page validates");
    CHECK(SP_GetFreeSpace(page, &free_after) == SP_OK, "free space after compaction");
    CHECK(free_after == (uint16_t)(free_before + sizeof(b) + sizeof(d)),
          "compaction reclaims exactly deleted record bytes");
    CHECK(get_counts(page, 4, 2), "compaction preserves slot/live counts");
    CHECK(check_record(page, sa, a, sizeof(a)), "A survives compaction at same slot");
    CHECK(check_record(page, sc, c, sizeof(c)), "C survives compaction at same slot");
    CHECK(SP_GetRecord(page, sb, a, sizeof(a), &free_before) == SP_ERR_INVALID_SLOT,
          "B remains deleted after compaction");
    CHECK(SP_GetRecord(page, sd, a, sizeof(a), &free_before) == SP_ERR_INVALID_SLOT,
          "D remains deleted after compaction");

    /* Compaction should be safe to repeat. */
    CHECK(SP_CompactPage(page) == SP_OK, "second compaction succeeds");
    CHECK(check_record(page, sa, a, sizeof(a)), "A survives second compaction");
    CHECK(check_record(page, sc, c, sizeof(c)), "C survives second compaction");
    return 1;
}

static int test_automatic_compaction_and_retry(void)
{
    char page[SP_PAGE_SIZE];
    unsigned char a[1000], b[1000], c[1000], replacement[1500];
    uint16_t sa, sb, sc, sr;
    uint16_t free_before;

    fill_bytes(a, sizeof(a), 51);
    fill_bytes(b, sizeof(b), 52);
    fill_bytes(c, sizeof(c), 53);
    fill_bytes(replacement, sizeof(replacement), 54);

    CHECK(SP_InitPage(page) == SP_OK, "initialize automatic compaction page");
    CHECK(SP_InsertRecord(page, a, sizeof(a), &sa) == SP_OK, "insert A");
    CHECK(SP_InsertRecord(page, b, sizeof(b), &sb) == SP_OK, "insert B");
    CHECK(SP_InsertRecord(page, c, sizeof(c), &sc) == SP_OK, "insert C");
    CHECK(SP_GetFreeSpace(page, &free_before) == SP_OK, "read contiguous free space");
    CHECK(free_before < sizeof(replacement), "precondition: replacement does not fit contiguously");
    CHECK(SP_DeleteRecord(page, sb) == SP_OK, "delete middle record B");

    /* This must compact automatically and retry inside SP_InsertRecord(). */
    CHECK(SP_InsertRecord(page, replacement, sizeof(replacement), &sr) == SP_OK,
          "automatic compaction and insertion retry");
    CHECK(sr == sb, "automatic retry reuses B's slot");
    CHECK(get_counts(page, 3, 3), "counts after automatic compaction and reuse");
    CHECK(SP_IsValidPage(page), "page validates after automatic compaction");
    CHECK(check_record(page, sa, a, sizeof(a)), "A survives automatic compaction");
    CHECK(check_record(page, sc, c, sizeof(c)), "C survives automatic compaction");
    CHECK(check_record(page, sr, replacement, sizeof(replacement)), "replacement stored correctly");
    return 1;
}

static int test_oversized_record_is_rejected_without_modification(void)
{
    char page[SP_PAGE_SIZE];
    char snapshot[SP_PAGE_SIZE];
    unsigned char too_large[SP_PAGE_SIZE];
    uint16_t slot = SP_INVALID_SLOT;

    fill_bytes(too_large, sizeof(too_large), 77);
    CHECK(SP_InitPage(page) == SP_OK, "initialize oversized-record page");
    memcpy(snapshot, page, sizeof(page));

    CHECK(SP_InsertRecord(page, too_large, SP_PAGE_SIZE, &slot) == SP_ERR_PAGE_FULL,
          "record requiring page plus slot overhead is rejected");
    CHECK(memcmp(page, snapshot, sizeof(page)) == 0,
          "failed oversized insertion leaves page unchanged");
    CHECK(slot == SP_INVALID_SLOT, "failed insertion does not publish a slot number");
    return 1;
}

static int test_exact_fit_and_then_full(void)
{
    char page[SP_PAGE_SIZE];
    unsigned char exact_fit[SP_PAGE_SIZE - sizeof(SPHeader) - sizeof(SPSlot)];
    unsigned char one_byte = 0xAB;
    uint16_t slot;

    fill_bytes(exact_fit, sizeof(exact_fit), 88);
    CHECK(SP_InitPage(page) == SP_OK, "initialize exact-fit page");
    CHECK(SP_InsertRecord(page, exact_fit, sizeof(exact_fit), &slot) == SP_OK,
          "record that exactly fills page is accepted");
    CHECK(SP_IsValidPage(page), "exact-fit page remains valid");
    CHECK(SP_InsertRecord(page, &one_byte, 1, &slot) == SP_ERR_PAGE_FULL,
          "additional byte is rejected on full page");
    return 1;
}

static int test_overlap_corruption_is_detected(void)
{
    char page[SP_PAGE_SIZE];
    unsigned char a[100], b[100];
    uint16_t sa, sb;
    uint16_t overlapping_offset = 3950;
    size_t slot1_offset = sizeof(SPHeader) + sizeof(SPSlot);

    fill_bytes(a, sizeof(a), 91);
    fill_bytes(b, sizeof(b), 92);
    CHECK(SP_InitPage(page) == SP_OK, "initialize overlap test page");
    CHECK(SP_InsertRecord(page, a, sizeof(a), &sa) == SP_OK, "insert A");
    CHECK(SP_InsertRecord(page, b, sizeof(b), &sb) == SP_OK, "insert B");
    CHECK(sa == 0 && sb == 1, "slots allocated as expected");

    /* Slot 1 becomes [3950,4050), overlapping slot 0's [3996,4096). */
    memcpy(page + slot1_offset, &overlapping_offset, sizeof(overlapping_offset));
    CHECK(!SP_IsValidPage(page), "overlapping live record ranges are rejected");
    return 1;
}

int main(void)
{
    struct TestCase {
        const char *name;
        int (*run)(void);
    } tests[] = {
        {"deletion and metadata counts", test_delete_and_counts},
        {"deleted-slot reuse without compaction", test_deleted_slot_reuse_without_compaction},
        {"explicit compaction and stable slots", test_explicit_compaction_preserves_slots},
        {"automatic compaction and retry", test_automatic_compaction_and_retry},
        {"oversized record rejection", test_oversized_record_is_rejected_without_modification},
        {"exact-fit and full-page boundary", test_exact_fit_and_then_full},
        {"overlapping-record corruption detection", test_overlap_corruption_is_detected}
    };
    size_t i;
    size_t passed = 0;
    size_t count = sizeof(tests) / sizeof(tests[0]);

    for (i = 0; i < count; ++i) {
        if (tests[i].run()) {
            printf("PASS: %s\n", tests[i].name);
            ++passed;
        } else {
            printf("FAIL: %s\n", tests[i].name);
            return EXIT_FAILURE;
        }
    }

    printf("\n%d/%d Phase 2 tests passed.\n", (int)passed, (int)count);
    return (passed == count) ? EXIT_SUCCESS : EXIT_FAILURE;
}