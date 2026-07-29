#ifndef FPS_PATCH_MEMORY_PATCH_H
#define FPS_PATCH_MEMORY_PATCH_H

#include "common.h"

enum {
    PATCH_TRANSACTION_CAPACITY = 32,
    PATCH_MAX_BYTES = 48
};

typedef struct PatchRecord {
    void *destination;
    size_t size;
    /* Enough for the largest indivisible replacement block. */
    u8 original[PATCH_MAX_BYTES];
} PatchRecord;

typedef struct PatchTransaction {
    /* Records successful writes in installation order for reverse rollback. */
    PatchRecord records[PATCH_TRANSACTION_CAPACITY];
    size_t count;
} PatchTransaction;

BOOL write_protected(void *destination, const void *source, size_t size);
void encode_u32(u8 *destination, u32 value);
void encode_rel32(u8 *operand, const u8 *next_instruction, const void *target);
void patch_transaction_init(PatchTransaction *transaction);
BOOL patch_transaction_write(PatchTransaction *transaction,
                             void *destination, const void *replacement, size_t size);
void patch_transaction_rollback(PatchTransaction *transaction);

#endif
