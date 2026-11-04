#include "static_store.h"

#include "pf_api.h"
#include "rid.h"

#include <string.h>

typedef char SRPFPageSizeCheck[
    (SR_PAGE_SIZE == PF_PAGE_SIZE) ? 1 : -1
];

static int srstore_find_last_page(SRStore *store)
{
    int page_num = -1;
    char *page_buf = NULL;
    int pf_status;

    store->has_current_page = 0;
    store->current_page = -1;
    store->current_count = 0;

    pf_status = PF_GetFirstPage(store->fd, &page_num, &page_buf);
    if (pf_status == PFE_EOF)
        return SRSTORE_OK;
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SRSTORE_ERR_PF;
    }

    for (;;) {
        uint16_t page_max_length;
        uint16_t page_count;
        int page_status;
        int next_page;

        if (!SR_IsValidPage(page_buf)) {
            (void)PF_UnfixPage(store->fd, page_num, FALSE);
            return SRSTORE_ERR_NOT_STATIC_PAGE;
        }

        page_status = SR_GetMaxRecordLength(page_buf, &page_max_length);
        if (page_status != SR_OK) {
            (void)PF_UnfixPage(store->fd, page_num, FALSE);
            return SRSTORE_ERR_NOT_STATIC_PAGE;
        }
        if (page_max_length != store->max_record_length) {
            (void)PF_UnfixPage(store->fd, page_num, FALSE);
            return SRSTORE_ERR_MAX_LENGTH_MISMATCH;
        }

        page_status = SR_GetRecordCount(page_buf, &page_count);
        if (page_status != SR_OK) {
            (void)PF_UnfixPage(store->fd, page_num, FALSE);
            return SRSTORE_ERR_NOT_STATIC_PAGE;
        }

        store->current_page = page_num;
        store->current_count = page_count;
        store->has_current_page = 1;

        pf_status = PF_UnfixPage(store->fd, page_num, FALSE);
        if (pf_status != PFE_OK) {
            store->last_pf_error = pf_status;
            return SRSTORE_ERR_PF;
        }

        next_page = page_num;
        pf_status = PF_GetNextPage(store->fd, &next_page, &page_buf);
        if (pf_status == PFE_EOF)
            return SRSTORE_OK;
        if (pf_status != PFE_OK) {
            store->last_pf_error = pf_status;
            return SRSTORE_ERR_PF;
        }
        page_num = next_page;
    }
}

int SRStore_CreateFile(const char *filename)
{
    if (filename == NULL || filename[0] == '\0')
        return SRSTORE_ERR_INVALID_ARGUMENT;
    if (PF_CreateFile((char *)filename) != PFE_OK)
        return SRSTORE_ERR_CREATE_FAILED;
    return SRSTORE_OK;
}

int SRStore_DestroyFile(const char *filename)
{
    if (filename == NULL || filename[0] == '\0')
        return SRSTORE_ERR_INVALID_ARGUMENT;
    if (PF_DestroyFile((char *)filename) != PFE_OK)
        return SRSTORE_ERR_PF;
    return SRSTORE_OK;
}

int SRStore_Open(SRStore *store, const char *filename,
                 uint16_t max_record_length)
{
    int fd;
    int status;
    uint32_t minimum_slot_size;

    if (store == NULL || filename == NULL || filename[0] == '\0' ||
        max_record_length == 0)
        return SRSTORE_ERR_INVALID_ARGUMENT;

    minimum_slot_size = (uint32_t)max_record_length + sizeof(uint16_t);
    if (minimum_slot_size > SR_PAGE_SIZE - sizeof(SRPageHeader))
        return SRSTORE_ERR_INVALID_ARGUMENT;

    memset(store, 0, sizeof(*store));
    store->fd = -1;
    store->current_page = -1;
    store->max_record_length = max_record_length;

    fd = PF_OpenFile((char *)filename);
    if (fd < 0) {
        store->last_pf_error = fd;
        return SRSTORE_ERR_OPEN_FAILED;
    }

    store->fd = fd;
    status = srstore_find_last_page(store);
    if (status != SRSTORE_OK) {
        (void)PF_CloseFile(store->fd);
        store->fd = -1;
        return status;
    }

    return SRSTORE_OK;
}

int SRStore_Close(SRStore *store)
{
    int status;

    if (store == NULL || store->fd < 0)
        return SRSTORE_ERR_INVALID_ARGUMENT;

    status = PF_CloseFile(store->fd);
    if (status != PFE_OK) {
        store->last_pf_error = status;
        return SRSTORE_ERR_CLOSE_FAILED;
    }

    store->fd = -1;
    store->current_page = -1;
    store->current_count = 0;
    store->has_current_page = 0;
    return SRSTORE_OK;
}

int SRStore_Insert(SRStore *store, const void *record,
                   uint16_t record_length, int *rid_out)
{
    int pf_status;
    int page_status;
    int page_num;
    char *page_buf = NULL;
    uint16_t slot_num;

    if (store == NULL || store->fd < 0 || record == NULL ||
        record_length == 0 || rid_out == NULL)
        return SRSTORE_ERR_INVALID_ARGUMENT;

    if (record_length > store->max_record_length)
        return SR_ERR_RECORD_TOO_LARGE;

    if (store->has_current_page) {
        page_num = store->current_page;
        pf_status = PF_GetThisPage(store->fd, page_num, &page_buf);
        if (pf_status != PFE_OK) {
            store->last_pf_error = pf_status;
            return SRSTORE_ERR_PF;
        }

        page_status = SR_InsertRecord(page_buf, record, record_length, &slot_num);
        if (page_status == SR_OK) {
            pf_status = PF_UnfixPage(store->fd, page_num, TRUE);
            if (pf_status != PFE_OK) {
                store->last_pf_error = pf_status;
                return SRSTORE_ERR_PF;
            }
            store->current_count = (uint16_t)(slot_num + 1U);
            if (SP_RID_Encode(page_num, slot_num, rid_out) != 0)
                return SRSTORE_ERR_INVALID_RID;
            return SRSTORE_OK;
        }

        pf_status = PF_UnfixPage(store->fd, page_num, FALSE);
        if (pf_status != PFE_OK) {
            store->last_pf_error = pf_status;
            return SRSTORE_ERR_PF;
        }

        if (page_status != SR_ERR_PAGE_FULL)
            return page_status;
    }

    pf_status = PF_AllocPage(store->fd, &page_num, &page_buf);
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SRSTORE_ERR_PF;
    }

    page_status = SR_InitPage(page_buf, store->max_record_length);
    if (page_status == SR_OK)
        page_status = SR_InsertRecord(page_buf, record, record_length, &slot_num);

    if (page_status != SR_OK) {
        pf_status = PF_UnfixPage(store->fd, page_num, FALSE);
        if (pf_status == PFE_OK)
            (void)PF_DisposePage(store->fd, page_num);
        else
            store->last_pf_error = pf_status;
        return page_status;
    }

    pf_status = PF_UnfixPage(store->fd, page_num, TRUE);
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SRSTORE_ERR_PF;
    }

    store->current_page = page_num;
    store->current_count = 1;
    store->has_current_page = 1;

    if (SP_RID_Encode(page_num, slot_num, rid_out) != 0)
        return SRSTORE_ERR_INVALID_RID;

    return SRSTORE_OK;
}

int SRStore_Get(SRStore *store, int rid, void *output,
                uint16_t output_capacity, uint16_t *record_length_out)
{
    int page_num;
    uint16_t slot_num;
    int pf_status;
    int page_status;
    char *page_buf = NULL;

    if (store == NULL || store->fd < 0 || output == NULL ||
        record_length_out == NULL)
        return SRSTORE_ERR_INVALID_ARGUMENT;

    if (SP_RID_Decode(rid, &page_num, &slot_num) != 0)
        return SRSTORE_ERR_INVALID_RID;

    pf_status = PF_GetThisPage(store->fd, page_num, &page_buf);
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SRSTORE_ERR_PF;
    }

    page_status = SR_GetRecord(page_buf, slot_num, output,
                               output_capacity, record_length_out);

    pf_status = PF_UnfixPage(store->fd, page_num, FALSE);
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SRSTORE_ERR_PF;
    }

    return page_status;
}

int SRStore_Scan(SRStore *store, SRStoreScanCallback callback,
                 void *context, uint64_t *records_scanned_out)
{
    int page_num = -1;
    char *page_buf = NULL;
    int pf_status;
    uint64_t delivered = 0;

    if (records_scanned_out != NULL)
        *records_scanned_out = 0;
    if (store == NULL || store->fd < 0 || callback == NULL)
        return SRSTORE_ERR_INVALID_ARGUMENT;

    pf_status = PF_GetFirstPage(store->fd, &page_num, &page_buf);
    if (pf_status == PFE_EOF)
        return SRSTORE_OK;
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SRSTORE_ERR_PF;
    }

    for (;;) {
        uint16_t count;
        uint16_t slot_num;
        int stop = 0;
        int page_status = SR_GetRecordCount(page_buf, &count);

        if (page_status != SR_OK) {
            (void)PF_UnfixPage(store->fd, page_num, FALSE);
            return page_status;
        }

        for (slot_num = 0; slot_num < count; ++slot_num) {
            unsigned char record[SR_PAGE_SIZE];
            uint16_t record_length = 0;
            int rid;
            int callback_status;

            page_status = SR_GetRecord(page_buf, slot_num, record,
                                       (uint16_t)sizeof(record), &record_length);
            if (page_status != SR_OK) {
                (void)PF_UnfixPage(store->fd, page_num, FALSE);
                return page_status;
            }
            if (SP_RID_Encode(page_num, slot_num, &rid) != 0) {
                (void)PF_UnfixPage(store->fd, page_num, FALSE);
                return SRSTORE_ERR_INVALID_RID;
            }

            callback_status = callback(rid, record, record_length, context);
            ++delivered;
            if (callback_status == SRSTORE_SCAN_STOP) {
                stop = 1;
                break;
            }
            if (callback_status != SRSTORE_SCAN_CONTINUE) {
                (void)PF_UnfixPage(store->fd, page_num, FALSE);
                if (records_scanned_out != NULL)
                    *records_scanned_out = delivered;
                return SRSTORE_ERR_SCAN_CALLBACK;
            }
        }

        pf_status = PF_UnfixPage(store->fd, page_num, FALSE);
        if (pf_status != PFE_OK) {
            store->last_pf_error = pf_status;
            if (records_scanned_out != NULL)
                *records_scanned_out = delivered;
            return SRSTORE_ERR_PF;
        }
        if (stop)
            break;

        pf_status = PF_GetNextPage(store->fd, &page_num, &page_buf);
        if (pf_status == PFE_EOF)
            break;
        if (pf_status != PFE_OK) {
            store->last_pf_error = pf_status;
            if (records_scanned_out != NULL)
                *records_scanned_out = delivered;
            return SRSTORE_ERR_PF;
        }
    }

    if (records_scanned_out != NULL)
        *records_scanned_out = delivered;
    return SRSTORE_OK;
}

int SRStore_GetStats(SRStore *store, SRStoreStats *stats_out)
{
    int page_num = -1;
    char *page_buf = NULL;
    int pf_status;
    SRStoreStats stats;

    if (store == NULL || store->fd < 0 || stats_out == NULL)
        return SRSTORE_ERR_INVALID_ARGUMENT;

    memset(&stats, 0, sizeof(stats));
    pf_status = PF_GetFirstPage(store->fd, &page_num, &page_buf);
    if (pf_status == PFE_EOF) {
        *stats_out = stats;
        return SRSTORE_OK;
    }
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SRSTORE_ERR_PF;
    }

    for (;;) {
        uint16_t count;
        uint16_t max_length;
        uint32_t used_bytes;
        uint32_t payload_bytes;
        int page_status;
        int next_page;

        page_status = SR_GetRecordCount(page_buf, &count);
        if (page_status == SR_OK)
            page_status = SR_GetMaxRecordLength(page_buf, &max_length);
        if (page_status == SR_OK)
            page_status = SR_GetUsedBytes(page_buf, &used_bytes);
        if (page_status == SR_OK)
            page_status = SR_GetPayloadBytes(page_buf, &payload_bytes);

        if (page_status != SR_OK) {
            (void)PF_UnfixPage(store->fd, page_num, FALSE);
            return page_status;
        }
        if (max_length != store->max_record_length) {
            (void)PF_UnfixPage(store->fd, page_num, FALSE);
            return SRSTORE_ERR_MAX_LENGTH_MISMATCH;
        }

        stats.page_count++;
        stats.record_count += count;
        stats.payload_bytes += payload_bytes;
        stats.record_slot_bytes += (uint64_t)count * max_length;
        stats.length_metadata_bytes += (uint64_t)count * sizeof(uint16_t);
        stats.page_header_bytes += sizeof(SRPageHeader);
        stats.occupied_bytes += used_bytes;
        stats.allocated_page_bytes += SR_PAGE_SIZE;

        pf_status = PF_UnfixPage(store->fd, page_num, FALSE);
        if (pf_status != PFE_OK) {
            store->last_pf_error = pf_status;
            return SRSTORE_ERR_PF;
        }

        next_page = page_num;
        pf_status = PF_GetNextPage(store->fd, &next_page, &page_buf);
        if (pf_status == PFE_EOF)
            break;
        if (pf_status != PFE_OK) {
            store->last_pf_error = pf_status;
            return SRSTORE_ERR_PF;
        }
        page_num = next_page;
    }

    stats.unused_bytes = stats.allocated_page_bytes - stats.occupied_bytes;
    stats.reserved_padding_bytes = stats.record_slot_bytes - stats.payload_bytes;
    /* PF stores one int nextfree field per page and two ints in its file header. */
    stats.pf_page_link_bytes = stats.page_count * (uint64_t)sizeof(int);
    stats.pf_file_header_bytes = 2U * (uint64_t)sizeof(int);
    stats.physical_file_bytes = stats.allocated_page_bytes +
                                stats.pf_page_link_bytes +
                                stats.pf_file_header_bytes;
    *stats_out = stats;
    return SRSTORE_OK;
}

const char *SRStore_ErrorString(int status)
{
    switch (status) {
        case SRSTORE_OK: return "success";
        case SRSTORE_ERR_INVALID_ARGUMENT: return "invalid argument";
        case SRSTORE_ERR_CREATE_FAILED: return "PF file creation failed";
        case SRSTORE_ERR_OPEN_FAILED: return "PF file open failed";
        case SRSTORE_ERR_CLOSE_FAILED: return "PF file close failed";
        case SRSTORE_ERR_PF: return "PF operation failed (see last_pf_error)";
        case SRSTORE_ERR_NOT_STATIC_PAGE: return "file contains a non-static or corrupt page";
        case SRSTORE_ERR_MAX_LENGTH_MISMATCH: return "static page maximum length differs from store configuration";
        case SRSTORE_ERR_INVALID_RID: return "invalid or unrepresentable RID";
        case SRSTORE_ERR_SCAN_CALLBACK: return "scan callback returned an error";
        default: return SR_ErrorString(status);
    }
}
