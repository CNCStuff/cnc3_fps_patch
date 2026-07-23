#include "memory_patch.h"

BOOL write_protected(void *destination, const void *source, size_t size) {
    DWORD old_protection;
    DWORD ignored;
    if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &old_protection)) {
        return FALSE;
    }
    memcpy(destination, source, size);
    /* Patched code can already be present in another core's instruction cache. */
    FlushInstructionCache(GetCurrentProcess(), destination, size);
    VirtualProtect(destination, size, old_protection, &ignored);
    return TRUE;
}

BOOL allocate_executable_stub(size_t size, u8 **out_stub) {
    void *memory;
    if (out_stub == NULL) {
        return FALSE;
    }
    memory = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (memory == NULL) {
        return FALSE;
    }
    *out_stub = (u8 *)memory;
    return TRUE;
}

BOOL finalize_executable_stub(u8 *stub, size_t size) {
    DWORD old_protection;
    if (!VirtualProtect(stub, size, PAGE_EXECUTE_READ, &old_protection)) {
        return FALSE;
    }
    /* Stubs are assembled while writable, then made W^X before publication. */
    return FlushInstructionCache(GetCurrentProcess(), stub, size);
}

void encode_u32(u8 *destination, u32 value) {
    destination[0] = (u8)value;
    destination[1] = (u8)(value >> 8);
    destination[2] = (u8)(value >> 16);
    destination[3] = (u8)(value >> 24);
}

void encode_rel32(u8 *operand, const u8 *next_instruction, const void *target) {
    /* x86 CALL/JMP displacement is relative to the end of the instruction. */
    intptr_t displacement = (const u8 *)target - next_instruction;
    encode_u32(operand, (u32)(i32)displacement);
}

void patch_transaction_init(PatchTransaction *transaction) {
    transaction->count = 0;
}

BOOL patch_transaction_write(PatchTransaction *transaction,
                             void *destination, const void *replacement, size_t size) {
    PatchRecord *record;
    if (transaction == NULL || destination == NULL || replacement == NULL || size == 0 ||
        size > PATCH_MAX_BYTES || transaction->count >= PATCH_TRANSACTION_CAPACITY) {
        return FALSE;
    }
    record = &transaction->records[transaction->count];
    record->destination = destination;
    record->size = size;
    memcpy(record->original, destination, size);
    if (!write_protected(destination, replacement, size)) return FALSE;
    /* Failed writes are not recorded; earlier completed writes remain rollbackable. */
    ++transaction->count;
    return TRUE;
}

void patch_transaction_rollback(PatchTransaction *transaction) {
    if (transaction == NULL) return;
    while (transaction->count != 0) {
        /* Reverse order also handles future overlapping/nested patch ranges safely. */
        PatchRecord *record = &transaction->records[--transaction->count];
        write_protected(record->destination, record->original, record->size);
    }
}
