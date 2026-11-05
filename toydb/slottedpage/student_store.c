#include "student_store.h"

#include "pf_api.h"
#include "rid.h"
#include "slotted_page.h"

#include <string.h>

/*
 * Find the last used PF page and verify that every used page has the
 * slotted-page format. This is page discovery for append recovery; it is
 * not the record-level sequential scan required in the next phase.
 */
static int spstore_find_last_page(SPStore *store)
{
    int page_num = -1;
    char *page_buf = NULL;
    int pf_status;

    store->has_current_page = 0;
    store->current_page = -1;

    pf_status = PF_GetFirstPage(store->fd, &page_num, &page_buf);
    if (pf_status == PFE_EOF)
        return SPSTORE_OK;
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SPSTORE_ERR_PF;
    }

    for (;;) {
        int next_page;

        if (!SP_IsValidPage(page_buf)) {
            (void)PF_UnfixPage(store->fd, page_num, FALSE);
            return SPSTORE_ERR_NOT_SLOTTED_PAGE;
        }

        store->current_page = page_num;
        store->has_current_page = 1;

        pf_status = PF_UnfixPage(store->fd, page_num, FALSE);
        if (pf_status != PFE_OK) {
            store->last_pf_error = pf_status;
            return SPSTORE_ERR_PF;
        }

        next_page = page_num;
        pf_status = PF_GetNextPage(store->fd, &next_page, &page_buf);
        if (pf_status == PFE_EOF)
            return SPSTORE_OK;
        if (pf_status != PFE_OK) {
            store->last_pf_error = pf_status;
            return SPSTORE_ERR_PF;
        }
        page_num = next_page;
    }
}

int SPStore_CreateFile(const char *filename)
{
    if (filename == NULL || filename[0] == '\0')
        return SPSTORE_ERR_INVALID_ARGUMENT;

    if (PF_CreateFile((char *)filename) != PFE_OK)
        return SPSTORE_ERR_CREATE_FAILED;

    return SPSTORE_OK;
}

int SPStore_DestroyFile(const char *filename)
{
    if (filename == NULL || filename[0] == '\0')
        return SPSTORE_ERR_INVALID_ARGUMENT;

    if (PF_DestroyFile((char *)filename) != PFE_OK)
        return SPSTORE_ERR_PF;

    return SPSTORE_OK;
}

int SPStore_Open(SPStore *store, const char *filename)
{
    int fd;
    int status;

    if (store == NULL || filename == NULL || filename[0] == '\0')
        return SPSTORE_ERR_INVALID_ARGUMENT;

    memset(store, 0, sizeof(*store));
    store->fd = -1;
    store->current_page = -1;

    fd = PF_OpenFile((char *)filename);
    if (fd < 0) {
        store->last_pf_error = fd;
        return SPSTORE_ERR_OPEN_FAILED;
    }

    store->fd = fd;
    status = spstore_find_last_page(store);
    if (status != SPSTORE_OK) {
        (void)PF_CloseFile(store->fd);
        store->fd = -1;
        return status;
    }

    return SPSTORE_OK;
}

int SPStore_Close(SPStore *store)
{
    int status;

    if (store == NULL || store->fd < 0)
        return SPSTORE_ERR_INVALID_ARGUMENT;

    status = PF_CloseFile(store->fd);
    if (status != PFE_OK) {
        store->last_pf_error = status;
        return SPSTORE_ERR_CLOSE_FAILED;
    }

    store->fd = -1;
    store->current_page = -1;
    store->has_current_page = 0;
    return SPSTORE_OK;
}

int SPStore_Insert(SPStore *store, const void *record,
                   uint16_t record_length, int *rid_out)
{
    int pf_status;
    int sp_status;
    int page_num;
    char *page_buf = NULL;
    uint16_t slot_num;

    if (store == NULL || store->fd < 0 || record == NULL ||
        record_length == 0 || rid_out == NULL)
        return SPSTORE_ERR_INVALID_ARGUMENT;

    /* Header + one descriptor + record must fit on an empty page. */
    if ((uint32_t)sizeof(SPHeader) + (uint32_t)sizeof(SPSlot) +
        (uint32_t)record_length > SP_PAGE_SIZE)
        return SPSTORE_ERR_RECORD_TOO_LARGE;

    if (store->has_current_page) {
        page_num = store->current_page;
        pf_status = PF_GetThisPage(store->fd, page_num, &page_buf);
        if (pf_status != PFE_OK) {
            store->last_pf_error = pf_status;
            return SPSTORE_ERR_PF;
        }

        sp_status = SP_InsertRecord(page_buf, record, record_length, &slot_num);
        if (sp_status == SP_OK) {
            pf_status = PF_UnfixPage(store->fd, page_num, TRUE);
            if (pf_status != PFE_OK) {
                store->last_pf_error = pf_status;
                return SPSTORE_ERR_PF;
            }
            if (SP_RID_Encode(page_num, slot_num, rid_out) != 0)
                return SPSTORE_ERR_INVALID_RID;
            return SPSTORE_OK;
        }

        pf_status = PF_UnfixPage(store->fd, page_num, FALSE);
        if (pf_status != PFE_OK) {
            store->last_pf_error = pf_status;
            return SPSTORE_ERR_PF;
        }

        if (sp_status != SP_ERR_PAGE_FULL)
            return sp_status;
    }

    pf_status = PF_AllocPage(store->fd, &page_num, &page_buf);
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SPSTORE_ERR_PF;
    }

    sp_status = SP_InitPage(page_buf);
    if (sp_status == SP_OK)
        sp_status = SP_InsertRecord(page_buf, record, record_length, &slot_num);

    /* Persist both the page initialization and inserted record. */
    pf_status = PF_UnfixPage(store->fd, page_num, TRUE);
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SPSTORE_ERR_PF;
    }
    if (sp_status != SP_OK)
        return sp_status;

    store->current_page = page_num;
    store->has_current_page = 1;

    if (SP_RID_Encode(page_num, slot_num, rid_out) != 0)
        return SPSTORE_ERR_INVALID_RID;

    return SPSTORE_OK;
}

int SPStore_Get(SPStore *store, int rid, void *output,
                uint16_t output_capacity, uint16_t *record_length_out)
{
    int page_num;
    uint16_t slot_num;
    int pf_status;
    int sp_status;
    char *page_buf = NULL;

    if (store == NULL || store->fd < 0 || output == NULL ||
        record_length_out == NULL)
        return SPSTORE_ERR_INVALID_ARGUMENT;

    if (SP_RID_Decode(rid, &page_num, &slot_num) != 0)
        return SPSTORE_ERR_INVALID_RID;

    pf_status = PF_GetThisPage(store->fd, page_num, &page_buf);
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SPSTORE_ERR_PF;
    }

    sp_status = SP_GetRecord(page_buf, slot_num, output,
                             output_capacity, record_length_out);

    pf_status = PF_UnfixPage(store->fd, page_num, FALSE);
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SPSTORE_ERR_PF;
    }

    return sp_status;
}

int SPStore_Delete(SPStore *store, int rid)
{
    int page_num;
    uint16_t slot_num;
    int pf_status;
    int sp_status;
    char *page_buf = NULL;

    if (store == NULL || store->fd < 0)
        return SPSTORE_ERR_INVALID_ARGUMENT;
    if (SP_RID_Decode(rid, &page_num, &slot_num) != 0)
        return SPSTORE_ERR_INVALID_RID;

    pf_status = PF_GetThisPage(store->fd, page_num, &page_buf);
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SPSTORE_ERR_PF;
    }

    sp_status = SP_DeleteRecord(page_buf, slot_num);
    pf_status = PF_UnfixPage(store->fd, page_num, sp_status == SP_OK);
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SPSTORE_ERR_PF;
    }

    return sp_status;
}

int SPStore_CompactPage(SPStore *store, int page_num)
{
    char *page_buf = NULL;
    int pf_status;
    int sp_status;

    if (store == NULL || store->fd < 0 || page_num < 0)
        return SPSTORE_ERR_INVALID_ARGUMENT;

    pf_status = PF_GetThisPage(store->fd, page_num, &page_buf);
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SPSTORE_ERR_PF;
    }

    sp_status = SP_CompactPage(page_buf);
    pf_status = PF_UnfixPage(store->fd, page_num, sp_status == SP_OK);
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SPSTORE_ERR_PF;
    }

    return sp_status;
}


/*
 * Sequentially visit every live record in every used PF page.
 * Each PF page is unfixed before requesting the next page, and every
 * error/early-stop path releases the currently fixed page.
 */
int SPStore_Scan(SPStore *store,
                 SPStoreScanCallback callback,
                 void *context,
                 uint64_t *records_scanned_out)
{
    int page_num = -1;
    char *page_buf = NULL;
    int pf_status;
    uint64_t delivered = 0;

    if (records_scanned_out != NULL)
        *records_scanned_out = 0;

    if (store == NULL || store->fd < 0 || callback == NULL)
        return SPSTORE_ERR_INVALID_ARGUMENT;

    pf_status = PF_GetFirstPage(store->fd, &page_num, &page_buf);
    if (pf_status == PFE_EOF)
        return SPSTORE_OK;
    if (pf_status != PFE_OK) {
        store->last_pf_error = pf_status;
        return SPSTORE_ERR_PF;
    }

    for (;;) {
        uint16_t slot_count = 0;
        uint16_t slot_num;
        int should_stop = 0;
        int page_error = SPSTORE_OK;

        /* GetSlotCount validates the complete slotted-page structure. */
        page_error = SP_GetSlotCount(page_buf, &slot_count);
        if (page_error != SP_OK) {
            int unfix_status = PF_UnfixPage(store->fd, page_num, FALSE);
            if (unfix_status != PFE_OK) {
                store->last_pf_error = unfix_status;
                if (records_scanned_out != NULL)
                    *records_scanned_out = delivered;
                return SPSTORE_ERR_PF;
            }
            if (records_scanned_out != NULL)
                *records_scanned_out = delivered;
            if (page_error == SP_ERR_BAD_PAGE)
                return SPSTORE_ERR_NOT_SLOTTED_PAGE;
            return page_error;
        }

        for (slot_num = 0; slot_num < slot_count; ++slot_num) {
            char record_buf[SP_PAGE_SIZE];
            uint16_t record_length = 0;
            int rid;
            int sp_status;
            int callback_status;

            sp_status = SP_GetRecord(page_buf, slot_num,
                                     record_buf, (uint16_t)sizeof(record_buf),
                                     &record_length);

            /* A tombstone is expected: skip it and continue scanning. */
            if (sp_status == SP_ERR_INVALID_SLOT)
                continue;

            if (sp_status != SP_OK) {
                int unfix_status = PF_UnfixPage(store->fd, page_num, FALSE);
                if (unfix_status != PFE_OK) {
                    store->last_pf_error = unfix_status;
                    if (records_scanned_out != NULL)
                        *records_scanned_out = delivered;
                    return SPSTORE_ERR_PF;
                }
                if (records_scanned_out != NULL)
                    *records_scanned_out = delivered;
                return sp_status;
            }

            if (SP_RID_Encode(page_num, slot_num, &rid) != 0) {
                int unfix_status = PF_UnfixPage(store->fd, page_num, FALSE);
                if (unfix_status != PFE_OK) {
                    store->last_pf_error = unfix_status;
                    if (records_scanned_out != NULL)
                        *records_scanned_out = delivered;
                    return SPSTORE_ERR_PF;
                }
                if (records_scanned_out != NULL)
                    *records_scanned_out = delivered;
                return SPSTORE_ERR_INVALID_RID;
            }

            callback_status = callback(rid, record_buf, record_length, context);
            ++delivered;
            if (callback_status == SPSTORE_SCAN_STOP) {
                should_stop = 1;
                break;
            }
            if (callback_status != SPSTORE_SCAN_CONTINUE) {
                int unfix_status = PF_UnfixPage(store->fd, page_num, FALSE);
                if (unfix_status != PFE_OK) {
                    store->last_pf_error = unfix_status;
                    return SPSTORE_ERR_PF;
                }
                if (records_scanned_out != NULL)
                    *records_scanned_out = delivered;
                return SPSTORE_ERR_SCAN_CALLBACK;
            }

        }

        /* Release this page before asking PF for the next one. */
        pf_status = PF_UnfixPage(store->fd, page_num, FALSE);
        if (pf_status != PFE_OK) {
            store->last_pf_error = pf_status;
            if (records_scanned_out != NULL)
                *records_scanned_out = delivered;
            return SPSTORE_ERR_PF;
        }

        if (should_stop)
            break;

        pf_status = PF_GetNextPage(store->fd, &page_num, &page_buf);
        if (pf_status == PFE_EOF)
            break;
        if (pf_status != PFE_OK) {
            store->last_pf_error = pf_status;
            if (records_scanned_out != NULL)
                *records_scanned_out = delivered;
            return SPSTORE_ERR_PF;
        }
    }

    if (records_scanned_out != NULL)
        *records_scanned_out = delivered;
    return SPSTORE_OK;
}

const char *SPStore_ErrorString(int status)
{
    switch (status) {
        case SPSTORE_OK:                   return "success";
        case SPSTORE_ERR_INVALID_ARGUMENT: return "invalid argument";
        case SPSTORE_ERR_CREATE_FAILED:    return "PF file creation failed";
        case SPSTORE_ERR_OPEN_FAILED:      return "PF file open failed";
        case SPSTORE_ERR_CLOSE_FAILED:     return "PF file close failed";
        case SPSTORE_ERR_PF:               return "PF operation failed (see last_pf_error)";
        case SPSTORE_ERR_NOT_SLOTTED_PAGE: return "file contains a non-slotted or corrupt page";
        case SPSTORE_ERR_RECORD_TOO_LARGE: return "record cannot fit in an empty slotted page";
        case SPSTORE_ERR_INVALID_RID:      return "invalid or unrepresentable RID";
        case SPSTORE_ERR_SCAN_CALLBACK:    return "record scan callback returned an error";
        default:                           return SP_ErrorString(status);
    }
}
