#ifndef KW_FPS_PATCH_LOG_H
#define KW_FPS_PATCH_LOG_H

#include "kw_common.h"

BOOL kw_log_open(const wchar_t *path, BOOL enabled);
void kw_log_close(void);
void kw_log_text(const char *text);
void kw_log_line(const char *text);
void kw_log_u32(const char *label, kw_u32 value);
void kw_log_hex32(const char *label, kw_u32 value);

#endif
