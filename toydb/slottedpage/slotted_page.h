#ifndef SLOTTED_PAGE_H
#define SLOTTED_PAGE_H

/*
 * Page-level slotted-page implementation for ToyDB.
 *
 * This module is deliberately independent of the PF layer.  It operates on
 * one caller-provided 4096-byte page buffer.  PF integration will be added in
 * a later stage through student_store.c.
 */

#include <stddef.h>
#include <stdint.h>

#define SP_PAGE_SIZE       4096U
#define SP_PAGE_MAGIC      0x53504C54U /* ASCII-like marker: "SPLT" */
#define SP_INVALID_SLOT    UINT16_MAX

/* Function return values. */
enum {
    SP_OK                    = 0,
    SP_ERR_INVALID_ARGUMENT  = -1,
    SP_ERR_BAD_PAGE          = -2,
    SP_ERR_PAGE_FULL         = -3,
    SP_ERR_INVALID_SLOT      = -4,
    SP_ERR_BUFFER_TOO_SMALL  = -5,
    SP_ERR_CORRUPT_PAGE      = -6
};

/*
 * Header stored at byte offset 0 of every slotted page.
 * The explicit-width integer types make the on-page layout predictable
 * within the supported ToyDB build environment.
 */
typedef struct SPHeader {
    uint32_t magic;
    uint16_t slot_count;
    uint16_t live_count;
    uint16_t free_start;
    uint16_t free_end;
} SPHeader;

/* One directory entry per slot. A zero length denotes a deleted slot. */
typedef struct SPSlot {
    uint16_t offset;
    uint16_t length;
} SPSlot;

/* Initialize all 4096 bytes and create an empty slotted page. */
int SP_InitPage(char *page);

/* Return nonzero when the page has a valid header and basic invariants. */
int SP_IsValidPage(const char *page);

/*
 * Insert a non-empty record into a page.
 *
 * On success, *slot_out receives a zero-based slot number.
 * Deleted slot descriptors are reused before a new slot is appended.
 * If contiguous space is insufficient but compaction can make the record fit,
 * SP_InsertRecord() compacts the page and retries automatically.
 * If the record cannot fit even after compaction, SP_ERR_PAGE_FULL is returned.
 */
int SP_InsertRecord(char *page,
                    const void *record,
                    uint16_t record_length,
                    uint16_t *slot_out);

/*
 * Retrive live record from a page by its zero-based slot number.
 * The caller supplies an output buffer and its capacity.
 * *record_length_out is set to the stored record length on success.
 */
int SP_GetRecord(const char *page,
                 uint16_t slot_num,
                 void *output,
                 uint16_t output_capacity,
                 uint16_t *record_length_out);

// Mark a slot as deleted however the slot number is retained
int SP_DeleteRecord(char *page, uint16_t slot_num);

// Pack all live records together (Compaction), while preserving slot numbers and slot count. */
int SP_CompactPage(char *page);

/* Read-only page metadata helpers. */
int SP_GetSlotCount(const char *page, uint16_t *slot_count_out);
int SP_GetLiveCount(const char *page, uint16_t *live_count_out);
int SP_GetFreeSpace(const char *page, uint16_t *free_space_out);

/* Convert an SP return code into a human-readable string. */
const char *SP_ErrorString(int error_code);

#endif /* SLOTTED_PAGE_H */
