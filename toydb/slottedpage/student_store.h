#ifndef TOYDB_STUDENT_STORE_H
#define TOYDB_STUDENT_STORE_H

#include <stdint.h>

/* Store-level errors are distinct from PF and slotted-page errors. */
enum {
    SPSTORE_OK = 0,
    SPSTORE_ERR_INVALID_ARGUMENT = -1000,
    SPSTORE_ERR_CREATE_FAILED = -1001,
    SPSTORE_ERR_OPEN_FAILED = -1002,
    SPSTORE_ERR_CLOSE_FAILED = -1003,
    SPSTORE_ERR_PF = -1004,
    SPSTORE_ERR_NOT_SLOTTED_PAGE = -1005,
    SPSTORE_ERR_RECORD_TOO_LARGE = -1006,
    SPSTORE_ERR_INVALID_RID = -1007,
    SPSTORE_ERR_SCAN_CALLBACK = -1008
};

/* Callback results for SPStore_Scan(). */
enum {
    SPSTORE_SCAN_CONTINUE = 0,
    SPSTORE_SCAN_STOP = 1
};

/*
 * The record buffer is valid only for the duration of the callback.
 * Copy the bytes if they must be retained after the callback returns.
 * The callback runs while the current PF page is fixed; do not close the
 * store or re-enter an operation that tries to fetch that same page.
 */
typedef int (*SPStoreScanCallback)(int rid,
                                   const void *record,
                                   uint16_t record_length,
                                   void *context);

typedef struct SPStore {
    int fd;
    int current_page;
    int has_current_page;
    int last_pf_error;
} SPStore;

/* PF_Init() must be called exactly once before the first SPStore_* call. */
int SPStore_CreateFile(const char *filename);
int SPStore_DestroyFile(const char *filename);
int SPStore_Open(SPStore *store, const char *filename);
int SPStore_Close(SPStore *store);

/* Append a record and return its encoded integer RID. */
int SPStore_Insert(SPStore *store, const void *record,
                   uint16_t record_length, int *rid_out);

/* Retrieve/delete a record using its encoded integer RID. */
int SPStore_Get(SPStore *store, int rid, void *output,
                uint16_t output_capacity, uint16_t *record_length_out);
int SPStore_Delete(SPStore *store, int rid);

/* Compact a specified PF page. The page number is not an RID. */
int SPStore_CompactPage(SPStore *store, int page_num);

/*
 * Scan all live records in PF page order and slot order.
 * Deleted/tombstone slots are skipped. A callback return of
 * SPSTORE_SCAN_STOP ends the scan successfully. Any other nonzero
 * callback result returns SPSTORE_ERR_SCAN_CALLBACK.
 * records_scanned_out may be NULL; otherwise it receives the number
 * of records delivered to the callback.
 */
int SPStore_Scan(SPStore *store,
                 SPStoreScanCallback callback,
                 void *context,
                 uint64_t *records_scanned_out);

const char *SPStore_ErrorString(int status);

#endif /* TOYDB_STUDENT_STORE_H */
