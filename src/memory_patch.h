#ifndef KW_FPS_PATCH_MEMORY_PATCH_H
#define KW_FPS_PATCH_MEMORY_PATCH_H

#include "kw_common.h"

BOOL kw_write_protected(void *destination, const void *source, size_t size);
BOOL kw_write_relative_branch(kw_u8 *instruction, kw_u8 opcode, const void *target);
BOOL kw_allocate_executable_stub(size_t size, kw_u8 **out_stub);
BOOL kw_finalize_executable_stub(kw_u8 *stub, size_t size);

#endif
