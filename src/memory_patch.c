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

BOOL kw_write_relative_branch(kw_u8 *instruction, kw_u8 opcode, const void *target) {
    kw_u8 patch[5];
    intptr_t displacement = (const kw_u8 *)target - (instruction + 5);
    patch[0] = opcode;
    *(kw_i32 *)&patch[1] = (kw_i32)displacement;
    return kw_write_protected(instruction, patch, sizeof(patch));
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
