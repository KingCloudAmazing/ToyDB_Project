#ifndef TOYDB_RID_H
#define TOYDB_RID_H

#include <limits.h>
#include <stdint.h>

/*
 * Encode a (PF page number, zero-based slot number) pair in the integer RID
 * expected by the supplied AM layer.
 *
 * Ten low bits are reserved for slot+1. Zero is reserved as an invalid RID
 * marker. With 4096-byte pages and four-byte slot entries, a valid slotted
 * page cannot approach the 1023-slot limit.
 */
#define SP_RID_SLOT_BITS 10U
#define SP_RID_SLOT_MASK ((1U << SP_RID_SLOT_BITS) - 1U)
#define SP_RID_MAX_SLOT  (SP_RID_SLOT_MASK - 1U)

static inline int SP_RID_Encode(int page_num, uint16_t slot_num, int *rid_out)
{
    uint32_t encoded;

    if (page_num < 0 || rid_out == 0 || slot_num > SP_RID_MAX_SLOT)
        return -1;

    if ((uint32_t)page_num > ((uint32_t)INT_MAX >> SP_RID_SLOT_BITS))
        return -1;

    encoded = ((uint32_t)page_num << SP_RID_SLOT_BITS) |
              ((uint32_t)slot_num + 1U);
    if (encoded == 0U || encoded > (uint32_t)INT_MAX)
        return -1;

    *rid_out = (int)encoded;
    return 0;
}

static inline int SP_RID_Decode(int rid, int *page_num_out,
                                uint16_t *slot_num_out)
{
    uint32_t value;
    uint32_t encoded_slot;

    if (rid <= 0 || page_num_out == 0 || slot_num_out == 0)
        return -1;

    value = (uint32_t)rid;
    encoded_slot = value & SP_RID_SLOT_MASK;
    if (encoded_slot == 0U)
        return -1;

    *page_num_out = (int)(value >> SP_RID_SLOT_BITS);
    *slot_num_out = (uint16_t)(encoded_slot - 1U);
    return 0;
}

#endif /* TOYDB_RID_H */
