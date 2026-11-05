#ifndef TOYDB_STATIC_STORE_H
#define TOYDB_STATIC_STORE_H

#include <stdint.h>

#include "static_page.h"

enum {
    SRSTORE_OK = 0,
    SRSTORE_ERR_INVALID_ARGUMENT = -2100,
    SRSTORE_ERR_CREATE_FAILED = -2101,
    SRSTORE_ERR_OPEN_FAILED = -2102,
    SRSTORE_ERR_CLOSE_FAILED = -2103,
    SRSTORE_ERR_PF = -2104,
    SRSTORE_ERR_NOT_STATIC_PAGE = -2105,
    SRSTORE_ERR_MAX_LENGTH_MISMATCH = -2106,
    SRSTORE_ERR_INVALID_RID = -2107,
    SRSTORE_ERR_SCAN_CALLBACK = -2108
};

enum {
    SRSTORE_SCAN_CONTINUE = 0,
    SRSTORE_SCAN_STOP = 1
};

typedef int (*SRStoreScanCallback)(int rid, const void *record,
                                   uint16_t record_length, void *context);

typedef struct SRStore {
    int fd;
    uint16_t max_record_length;
    int current_page;
    uint16_t current_count;
    int has_current_page;
    int last_pf_error;
} SRStore;

typedef struct SRStoreStats {
    uint64_t page_count;
    uint64_t record_count;
    uint64_t payload_bytes;
    uint64_t record_slot_bytes;
    uint64_t length_metadata_bytes;
    uint64_t page_header_bytes;
    uint64_t occupied_bytes;
    uint64_t allocated_page_bytes;
    uint64_t unused_bytes;
    uint64_t reserved_padding_bytes;
    uint64_t pf_page_link_bytes;
    uint64_t pf_file_header_bytes;
    uint64_t physical_file_bytes;
} SRStoreStats;

int SRStore_CreateFile(const char *filename);
int SRStore_DestroyFile(const char *filename);
int SRStore_Open(SRStore *store, const char *filename,
                 uint16_t max_record_length);
int SRStore_Close(SRStore *store);

int SRStore_Insert(SRStore *store, const void *record,
                   uint16_t record_length, int *rid_out);
int SRStore_Get(SRStore *store, int rid, void *output,
                uint16_t output_capacity, uint16_t *record_length_out);

int SRStore_Scan(SRStore *store, SRStoreScanCallback callback,
                 void *context, uint64_t *records_scanned_out);
int SRStore_GetStats(SRStore *store, SRStoreStats *stats_out);

const char *SRStore_ErrorString(int status);

#endif /* TOYDB_STATIC_STORE_H */
