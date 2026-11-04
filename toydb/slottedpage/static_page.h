#ifndef TOYDB_STATIC_PAGE_H
#define TOYDB_STATIC_PAGE_H

#include <stdint.h>

#define SR_PAGE_SIZE 4096u
#define SR_PAGE_MAGIC 0x53525047u  /* "SRPG" */

/*
 * Static-page header:
 * magic              : 4 bytes
 * max_record_length  : 2 bytes
 * record_count       : 2 bytes
 *
 * Total: 8 bytes.
 */
typedef struct {
    uint32_t magic;
    uint16_t max_record_length;
    uint16_t record_count;
} SRPageHeader;

/* Ensure the page header has the expected size. */
typedef char SRHeaderSizeCheck[
    sizeof(SRPageHeader) == 8 ? 1 : -1
];

enum {
    SR_OK = 0,
    SR_ERR_INVALID_ARGUMENT = -2000,
    SR_ERR_BAD_PAGE = -2001,
    SR_ERR_PAGE_FULL = -2002,
    SR_ERR_RECORD_TOO_LARGE = -2003,
    SR_ERR_INVALID_SLOT = -2004,
    SR_ERR_BUFFER_TOO_SMALL = -2005
};

/* Initialize and validate a static-record page. */
int SR_InitPage(void *page, uint16_t max_record_length);
int SR_IsValidPage(const void *page);

/* Insert and retrieve fixed-slot records. */
int SR_InsertRecord(void *page,
                    const void *record,
                    uint16_t record_length,
                    uint16_t *slot_out);

int SR_GetRecord(const void *page,
                 uint16_t slot_num,
                 void *output,
                 uint16_t output_capacity,
                 uint16_t *record_length_out);

/* Page information for storage measurements. */
int SR_GetRecordCount(const void *page, uint16_t *count_out);
int SR_GetCapacity(const void *page, uint16_t *capacity_out);
int SR_GetMaxRecordLength(const void *page, uint16_t *max_length_out);
int SR_GetUsedBytes(const void *page, uint32_t *used_bytes_out);
int SR_GetPayloadBytes(const void *page, uint32_t *payload_bytes_out);

const char *SR_ErrorString(int status);

#endif