#ifndef KW_FPS_PATCH_MEMORY_PATCH_H
#define KW_FPS_PATCH_MEMORY_PATCH_H

#include "kw_common.h"

enum {
    KW_PATCH_TRANSACTION_CAPACITY = 16,
    KW_PATCH_MAX_BYTES = 16
};

typedef struct KwPatchRecord {
    void *destination;
    size_t size;
    kw_u8 original[KW_PATCH_MAX_BYTES];
} KwPatchRecord;

typedef struct KwPatchTransaction {
    KwPatchRecord records[KW_PATCH_TRANSACTION_CAPACITY];
    size_t count;
} KwPatchTransaction;

BOOL kw_write_protected(void *destination, const void *source, size_t size);
BOOL kw_allocate_executable_stub(size_t size, kw_u8 **out_stub);
BOOL kw_finalize_executable_stub(kw_u8 *stub, size_t size);
void kw_encode_u32(kw_u8 *destination, kw_u32 value);
void kw_encode_rel32(kw_u8 *operand, const kw_u8 *next_instruction, const void *target);
void kw_patch_transaction_init(KwPatchTransaction *transaction);
BOOL kw_patch_transaction_write(KwPatchTransaction *transaction,
                                void *destination, const void *replacement, size_t size);
void kw_patch_transaction_rollback(KwPatchTransaction *transaction);

#endif
