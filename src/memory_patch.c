#include "memory_patch.h"

BOOL kw_write_protected(void *destination, const void *source, size_t size) {
    DWORD old_protection;
    DWORD ignored;
    if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &old_protection)) {
        return FALSE;
    }
    memcpy(destination, source, size);
    FlushInstructionCache(GetCurrentProcess(), destination, size);
    VirtualProtect(destination, size, old_protection, &ignored);
    return TRUE;
}

BOOL kw_allocate_executable_stub(size_t size, kw_u8 **out_stub) {
    void *memory;
    if (out_stub == NULL) {
        return FALSE;
    }
    memory = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (memory == NULL) {
        return FALSE;
    }
    *out_stub = (kw_u8 *)memory;
    return TRUE;
}

BOOL kw_finalize_executable_stub(kw_u8 *stub, size_t size) {
    DWORD old_protection;
    if (!VirtualProtect(stub, size, PAGE_EXECUTE_READ, &old_protection)) {
        return FALSE;
    }
    return FlushInstructionCache(GetCurrentProcess(), stub, size);
}

void kw_encode_u32(kw_u8 *destination, kw_u32 value) {
    destination[0] = (kw_u8)value;
    destination[1] = (kw_u8)(value >> 8);
    destination[2] = (kw_u8)(value >> 16);
    destination[3] = (kw_u8)(value >> 24);
}

void kw_encode_rel32(kw_u8 *operand, const kw_u8 *next_instruction, const void *target) {
    intptr_t displacement = (const kw_u8 *)target - next_instruction;
    kw_encode_u32(operand, (kw_u32)(kw_i32)displacement);
}

void kw_patch_transaction_init(KwPatchTransaction *transaction) {
    transaction->count = 0;
}

BOOL kw_patch_transaction_write(KwPatchTransaction *transaction,
                                void *destination, const void *replacement, size_t size) {
    KwPatchRecord *record;
    if (transaction == NULL || destination == NULL || replacement == NULL || size == 0 ||
        size > KW_PATCH_MAX_BYTES || transaction->count >= KW_PATCH_TRANSACTION_CAPACITY) {
        return FALSE;
    }
    record = &transaction->records[transaction->count];
    record->destination = destination;
    record->size = size;
    memcpy(record->original, destination, size);
    if (!kw_write_protected(destination, replacement, size)) return FALSE;
    ++transaction->count;
    return TRUE;
}

void kw_patch_transaction_rollback(KwPatchTransaction *transaction) {
    if (transaction == NULL) return;
    while (transaction->count != 0) {
        KwPatchRecord *record = &transaction->records[--transaction->count];
        kw_write_protected(record->destination, record->original, record->size);
    }
}
