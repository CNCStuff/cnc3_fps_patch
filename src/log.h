#ifndef FPS_PATCH_LOG_H
#define FPS_PATCH_LOG_H

#include "common.h"

BOOL log_open(const wchar_t *path, BOOL enabled);
void log_close(void);
void log_text(const char *text);
void log_line(const char *text);
void log_u32(const char *label, u32 value);
void log_hex32(const char *label, u32 value);

#endif
