#include "static_page.h"

#include <stddef.h>
#include <string.h>

/*
 * Each fixed-size record slot contains:
 *
 * +----------------------+------------------------+
 * | Actual length (2 B)  | Reserved data (M bytes) |
 * +----------------------+------------------------+
 *
 * M is configured when the page is initialized.
 */

static uint16_t sr_capacity(const SRPageHeader *header)
{
    uint32_t slot_size;
    uint32_t available_bytes;

    slot_size = (uint32_t)header->max_record_length + sizeof(uint16_t);
    available_bytes = SR_PAGE_SIZE - sizeof(SRPageHeader);

    if (slot_size == 0)
        return 0;

    return (uint16_t)(available_bytes / slot_size);
}

static uint32_t sr_slot_offset(const SRPageHeader *header,
                               uint16_t slot_num)
{
    uint32_t slot_size;

    slot_size = (uint32_t)header->max_record_length + sizeof(uint16_t);

    return (uint32_t)sizeof(SRPageHeader) +
           (uint32_t)slot_num * slot_size;
}

/*
 * Read and validate the page header and existing records.
 * All page access uses memcpy to avoid alignment assumptions.
 */
static int sr_read_header(const void *page, SRPageHeader *header)
{
    uint16_t i;
    uint16_t capacity;
    const unsigned char *bytes;

    if (page == NULL || header == NULL)
        return SR_ERR_INVALID_ARGUMENT;

    bytes = (const unsigned char *)page;
    memcpy(header, bytes, sizeof(*header));

    if (header->magic != SR_PAGE_MAGIC ||
        header->max_record_length == 0)
        return SR_ERR_BAD_PAGE;

    capacity = sr_capacity(header);

    if (capacity == 0 || header->record_count > capacity)
        return SR_ERR_BAD_PAGE;

    /* Validate all existing record-length fields. */
    for (i = 0; i < header->record_count; ++i) {
        uint16_t stored_length;
        uint32_t offset = sr_slot_offset(header, i);

        memcpy(&stored_length, bytes + offset, sizeof(stored_length));

        if (stored_length == 0 ||
            stored_length > header->max_record_length)
            return SR_ERR_BAD_PAGE;
    }

    return SR_OK;
}

int SR_InitPage(void *page, uint16_t max_record_length)
{
    SRPageHeader header;
    uint32_t slot_size;

    if (page == NULL || max_record_length == 0)
        return SR_ERR_INVALID_ARGUMENT;

    slot_size = (uint32_t)max_record_length + sizeof(uint16_t);

    if (slot_size > SR_PAGE_SIZE - sizeof(SRPageHeader))
        return SR_ERR_INVALID_ARGUMENT;

    memset(page, 0, SR_PAGE_SIZE);

    header.magic = SR_PAGE_MAGIC;
    header.max_record_length = max_record_length;
    header.record_count = 0;

    memcpy(page, &header, sizeof(header));

    return SR_OK;
}

int SR_IsValidPage(const void *page)
{
    SRPageHeader header;

    return sr_read_header(page, &header) == SR_OK;
}

int SR_InsertRecord(void *page,
                    const void *record,
                    uint16_t record_length,
                    uint16_t *slot_out)
{
    SRPageHeader header;
    unsigned char *bytes;
    unsigned char *slot;
    uint32_t offset;
    uint32_t slot_size;
    int status;

    if (page == NULL || record == NULL ||
        record_length == 0 || slot_out == NULL)
        return SR_ERR_INVALID_ARGUMENT;

    status = sr_read_header(page, &header);
    if (status != SR_OK)
        return status;

    if (record_length > header.max_record_length)
        return SR_ERR_RECORD_TOO_LARGE;

    if (header.record_count >= sr_capacity(&header))
        return SR_ERR_PAGE_FULL;

    offset = sr_slot_offset(&header, header.record_count);
    slot_size = (uint32_t)header.max_record_length + sizeof(uint16_t);

    bytes = (unsigned char *)page;
    slot = bytes + offset;

    /*
     * Clear the complete fixed-size slot.
     * This makes unused padding deterministic.
     */
    memset(slot, 0, slot_size);

    memcpy(slot, &record_length, sizeof(record_length));
    memcpy(slot + sizeof(record_length), record, record_length);

    *slot_out = header.record_count;
    header.record_count++;

    memcpy(page, &header, sizeof(header));

    return SR_OK;
}

int SR_GetRecord(const void *page,
                 uint16_t slot_num,
                 void *output,
                 uint16_t output_capacity,
                 uint16_t *record_length_out)
{
    SRPageHeader header;
    const unsigned char *bytes;
    const unsigned char *slot;
    uint16_t stored_length;
    uint32_t offset;
    int status;

    if (page == NULL || output == NULL ||
        record_length_out == NULL)
        return SR_ERR_INVALID_ARGUMENT;

    status = sr_read_header(page, &header);
    if (status != SR_OK)
        return status;

    if (slot_num >= header.record_count)
        return SR_ERR_INVALID_SLOT;

    offset = sr_slot_offset(&header, slot_num);
    bytes = (const unsigned char *)page;
    slot = bytes + offset;

    memcpy(&stored_length, slot, sizeof(stored_length));

    *record_length_out = stored_length;

    if (output_capacity < stored_length)
        return SR_ERR_BUFFER_TOO_SMALL;

    memcpy(output, slot + sizeof(stored_length), stored_length);

    return SR_OK;
}

int SR_GetRecordCount(const void *page, uint16_t *count_out)
{
    SRPageHeader header;
    int status;

    if (count_out == NULL)
        return SR_ERR_INVALID_ARGUMENT;

    status = sr_read_header(page, &header);
    if (status != SR_OK)
        return status;

    *count_out = header.record_count;
    return SR_OK;
}

int SR_GetCapacity(const void *page, uint16_t *capacity_out)
{
    SRPageHeader header;
    int status;

    if (capacity_out == NULL)
        return SR_ERR_INVALID_ARGUMENT;

    status = sr_read_header(page, &header);
    if (status != SR_OK)
        return status;

    *capacity_out = sr_capacity(&header);
    return SR_OK;
}

int SR_GetMaxRecordLength(const void *page, uint16_t *max_length_out)
{
    SRPageHeader header;
    int status;

    if (max_length_out == NULL)
        return SR_ERR_INVALID_ARGUMENT;
    status = sr_read_header(page, &header);
    if (status != SR_OK)
        return status;
    *max_length_out = header.max_record_length;
    return SR_OK;
}

/*
 * Bytes occupied by the page header and the allocated fixed slots.
 * This does not count unused trailing space in the page.
 */
int SR_GetUsedBytes(const void *page, uint32_t *used_bytes_out)
{
    SRPageHeader header;
    uint32_t slot_size;
    int status;

    if (used_bytes_out == NULL)
        return SR_ERR_INVALID_ARGUMENT;

    status = sr_read_header(page, &header);
    if (status != SR_OK)
        return status;

    slot_size = (uint32_t)header.max_record_length + sizeof(uint16_t);

    *used_bytes_out = (uint32_t)sizeof(SRPageHeader) +
                      (uint32_t)header.record_count * slot_size;

    return SR_OK;
}

/* Actual payload bytes, excluding length prefixes and reserved padding. */
int SR_GetPayloadBytes(const void *page, uint32_t *payload_bytes_out)
{
    SRPageHeader header;
    const unsigned char *bytes;
    uint16_t i;
    uint32_t total = 0;
    int status;

    if (payload_bytes_out == NULL)
        return SR_ERR_INVALID_ARGUMENT;

    status = sr_read_header(page, &header);
    if (status != SR_OK)
        return status;

    bytes = (const unsigned char *)page;

    for (i = 0; i < header.record_count; ++i) {
        uint16_t stored_length;
        uint32_t offset = sr_slot_offset(&header, i);

        memcpy(&stored_length, bytes + offset, sizeof(stored_length));
        total += stored_length;
    }

    *payload_bytes_out = total;
    return SR_OK;
}

const char *SR_ErrorString(int status)
{
    switch (status) {
        case SR_OK:
            return "success";

        case SR_ERR_INVALID_ARGUMENT:
            return "invalid argument";

        case SR_ERR_BAD_PAGE:
            return "invalid or corrupted static page";

        case SR_ERR_PAGE_FULL:
            return "static page is full";

        case SR_ERR_RECORD_TOO_LARGE:
            return "record exceeds configured maximum length";

        case SR_ERR_INVALID_SLOT:
            return "invalid slot number";

        case SR_ERR_BUFFER_TOO_SMALL:
            return "output buffer is too small";

        default:
            return "unknown static-page error";
    }
}