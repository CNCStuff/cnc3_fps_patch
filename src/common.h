#ifndef FPS_PATCH_COMMON_H
#define FPS_PATCH_COMMON_H

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
#error "fps_patch must be built for 32-bit Windows (x86-windows)."
#endif

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define STDCALL __attribute__((stdcall))
#define THISCALL __attribute__((thiscall))
#define NOINLINE __attribute__((noinline))

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t i32;

void *memcpy(void *destination, const void *source, size_t size);
void *memmove(void *destination, const void *source, size_t size);
void *memset(void *destination, int value, size_t size);
int memcmp(const void *left, const void *right, size_t size);

u32 load_u32(const void *address);
size_t ascii_length(const char *text);
size_t wide_length(const wchar_t *text);
BOOL wide_copy(wchar_t *destination, size_t capacity, const wchar_t *source);
BOOL wide_append(wchar_t *destination, size_t capacity, const wchar_t *suffix);
BOOL path_replace_filename(wchar_t *path, size_t capacity, const wchar_t *filename);

#endif
