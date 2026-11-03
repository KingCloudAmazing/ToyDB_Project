#include "slotted_page.h"

#include <string.h>

/* Keep page offsets independent of compiler-specific struct padding. */
#define SP_HEADER_SIZE ((uint16_t)sizeof(SPHeader))
#define SP_SLOT_SIZE   ((uint16_t)sizeof(SPSlot))

/* Copy structures in/out instead of assuming arbitrary char buffers are aligned. */
static void sp_load_header(const char *page, SPHeader *header)
{
    memcpy(header, page, sizeof(*header));
}

static void sp_store_header(char *page, const SPHeader *header)
{
    memcpy(page, header, sizeof(*header));
}

static void sp_load_slot(const char *page, uint16_t slot_num, SPSlot *slot)
{
    size_t slot_offset = sizeof(SPHeader) +
                         ((size_t)slot_num * sizeof(SPSlot));
    memcpy(slot, page + slot_offset, sizeof(*slot));
}

static void sp_store_slot(char *page, uint16_t slot_num, const SPSlot *slot)
{
    size_t slot_offset = sizeof(SPHeader) +
                         ((size_t)slot_num * sizeof(SPSlot));
    memcpy(page + slot_offset, slot, sizeof(*slot));
}

/*
 * Validate the page header and the basic bounds of every live slot.
 * Overlap checking is intentionally omitted here; it will be added to the
 * deeper validator when deletion/compaction is implemented.
 */
static int sp_validate_page(const char *page, SPHeader *header_out)
{
    SPHeader header;
    uint32_t expected_free_start;
    uint16_t i;
    uint16_t counted_live = 0;

    if (page == NULL)
        return SP_ERR_INVALID_ARGUMENT;

    sp_load_header(page, &header);

    if (header.magic != SP_PAGE_MAGIC)
        return SP_ERR_BAD_PAGE;

    expected_free_start = (uint32_t)sizeof(SPHeader) +
                          ((uint32_t)header.slot_count * sizeof(SPSlot));

    if (expected_free_start > SP_PAGE_SIZE)
        return SP_ERR_CORRUPT_PAGE;

    if (header.free_start != expected_free_start)
        return SP_ERR_CORRUPT_PAGE;

    if (header.free_end > SP_PAGE_SIZE ||
        header.free_end < header.free_start)
        return SP_ERR_CORRUPT_PAGE;

    if (header.live_count > header.slot_count)
        return SP_ERR_CORRUPT_PAGE;

    for (i = 0; i < header.slot_count; ++i) {
        SPSlot slot;
        uint32_t slot_end;

        sp_load_slot(page, i, &slot);

        if (slot.length == 0) {
            if (slot.offset != 0)
                return SP_ERR_CORRUPT_PAGE;
            continue;
        }

        ++counted_live;
        slot_end = (uint32_t)slot.offset + (uint32_t)slot.length;

        if (slot.offset < header.free_end || slot_end > SP_PAGE_SIZE)
            return SP_ERR_CORRUPT_PAGE;
    }

    if (counted_live != header.live_count)
        return SP_ERR_CORRUPT_PAGE;

    if (header_out != NULL)
        *header_out = header;

    return SP_OK;
}

int SP_InitPage(char *page)
{
    SPHeader header;

    if (page == NULL)
        return SP_ERR_INVALID_ARGUMENT;

    /* PF_AllocPage does not clear the returned page in the supplied code. */
    memset(page, 0, SP_PAGE_SIZE);

    header.magic = SP_PAGE_MAGIC;
    header.slot_count = 0;
    header.live_count = 0;
    header.free_start = SP_HEADER_SIZE;
    header.free_end = (uint16_t)SP_PAGE_SIZE;

    sp_store_header(page, &header);
    return SP_OK;
}

int SP_IsValidPage(const char *page)
{
    return sp_validate_page(page, NULL) == SP_OK;
}

int SP_InsertRecord(char *page,
                    const void *record,
                    uint16_t record_length,
                    uint16_t *slot_out)
{
    SPHeader header;
    SPSlot slot;
    uint16_t new_offset;
    uint16_t new_slot_num;
    uint32_t required_space;
    int status;

    if (page == NULL || record == NULL || slot_out == NULL || record_length == 0)
        return SP_ERR_INVALID_ARGUMENT;

    status = sp_validate_page(page, &header);
    if (status != SP_OK)
        return status;

    /* This stage appends new slots. Reuse of deleted slots comes later. */
    required_space = (uint32_t)record_length + sizeof(SPSlot);

    if ((uint32_t)header.free_end < required_space ||
        ((uint32_t)header.free_end - required_space) < header.free_start)
        return SP_ERR_PAGE_FULL;

    if (header.slot_count == UINT16_MAX || header.live_count == UINT16_MAX)
        return SP_ERR_PAGE_FULL;

    new_offset = (uint16_t)(header.free_end - record_length);
    new_slot_num = header.slot_count;

    /* Store record bytes first, then the slot directory entry and header. */
    memcpy(page + new_offset, record, record_length);

    slot.offset = new_offset;
    slot.length = record_length;
    sp_store_slot(page, new_slot_num, &slot);

    header.slot_count++;
    header.live_count++;
    header.free_start = (uint16_t)(header.free_start + sizeof(SPSlot));
    header.free_end = new_offset;

    sp_store_header(page, &header);
    *slot_out = new_slot_num;

    return SP_OK;
}

int SP_GetRecord(const char *page,
                 uint16_t slot_num,
                 void *output,
                 uint16_t output_capacity,
                 uint16_t *record_length_out)
{
    SPHeader header;
    SPSlot slot;
    int status;

    if (page == NULL || output == NULL || record_length_out == NULL)
        return SP_ERR_INVALID_ARGUMENT;

    status = sp_validate_page(page, &header);
    if (status != SP_OK)
        return status;

    if (slot_num >= header.slot_count)
        return SP_ERR_INVALID_SLOT;

    sp_load_slot(page, slot_num, &slot);

    if (slot.length == 0)
        return SP_ERR_INVALID_SLOT;

    if (output_capacity < slot.length)
        return SP_ERR_BUFFER_TOO_SMALL;

    memcpy(output, page + slot.offset, slot.length);
    *record_length_out = slot.length;

    return SP_OK;
}

int SP_GetSlotCount(const char *page, uint16_t *slot_count_out)
{
    SPHeader header;
    int status;

    if (slot_count_out == NULL)
        return SP_ERR_INVALID_ARGUMENT;

    status = sp_validate_page(page, &header);
    if (status != SP_OK)
        return status;

    *slot_count_out = header.slot_count;
    return SP_OK;
}

int SP_GetLiveCount(const char *page, uint16_t *live_count_out)
{
    SPHeader header;
    int status;

    if (live_count_out == NULL)
        return SP_ERR_INVALID_ARGUMENT;

    status = sp_validate_page(page, &header);
    if (status != SP_OK)
        return status;

    *live_count_out = header.live_count;
    return SP_OK;
}

int SP_GetFreeSpace(const char *page, uint16_t *free_space_out)
{
    SPHeader header;
    int status;

    if (free_space_out == NULL)
        return SP_ERR_INVALID_ARGUMENT;

    status = sp_validate_page(page, &header);
    if (status != SP_OK)
        return status;

    *free_space_out = (uint16_t)(header.free_end - header.free_start);
    return SP_OK;
}

const char *SP_ErrorString(int error_code)
{
    switch (error_code) {
        case SP_OK:                   return "success";
        case SP_ERR_INVALID_ARGUMENT: return "invalid argument";
        case SP_ERR_BAD_PAGE:         return "page is not initialized as a slotted page";
        case SP_ERR_PAGE_FULL:        return "page has insufficient free space";
        case SP_ERR_INVALID_SLOT:     return "slot number is invalid or deleted";
        case SP_ERR_BUFFER_TOO_SMALL: return "output buffer is too small";
        case SP_ERR_CORRUPT_PAGE:     return "slotted page metadata is inconsistent";
        default:                      return "unknown slotted-page error";
    }
}
