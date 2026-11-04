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
    SPSTORE_ERR_INVALID_RID = -1007
};

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

const char *SPStore_ErrorString(int status);

#endif /* TOYDB_STUDENT_STORE_H */
