#ifndef KW_FPS_PATCH_COMMON_H
#define KW_FPS_PATCH_COMMON_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define DIRECTINPUT_VERSION 0x0800

#include <windows.h>
#include <dinput.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stddef.h>

#if !defined(_WIN32) || !defined(__i386__)
#error "kw_fps_patch must be built for 32-bit Windows (x86-windows)."
#endif

#define KW_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define KW_STDCALL __attribute__((stdcall))
#define KW_THISCALL __attribute__((thiscall))
#define KW_NOINLINE __attribute__((noinline))

typedef uint8_t kw_u8;
typedef uint16_t kw_u16;
typedef uint32_t kw_u32;
typedef uint64_t kw_u64;
typedef int32_t kw_i32;

typedef struct KwConfig {
    BOOL enabled;
    kw_u32 target_fps;
    BOOL precise_pacing;
    kw_u32 spin_threshold_us;
    BOOL logging;
} KwConfig;

extern HMODULE g_kw_self_module;
extern kw_u8 *g_kw_game_module;
extern KwConfig g_kw_config;

void *memcpy(void *destination, const void *source, size_t size);
void *memmove(void *destination, const void *source, size_t size);
void *memset(void *destination, int value, size_t size);
int memcmp(const void *left, const void *right, size_t size);

size_t kw_ascii_length(const char *text);
size_t kw_wide_length(const wchar_t *text);
BOOL kw_wide_copy(wchar_t *destination, size_t capacity, const wchar_t *source);
BOOL kw_wide_append(wchar_t *destination, size_t capacity, const wchar_t *suffix);
BOOL kw_path_replace_filename(wchar_t *path, size_t capacity, const wchar_t *filename);

#endif
