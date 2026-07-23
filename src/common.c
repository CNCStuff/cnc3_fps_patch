#include "common.h"

void *memcpy(void *destination, const void *source, size_t size) {
    u8 *out = (u8 *)destination;
    const u8 *in = (const u8 *)source;
    while (size-- != 0) {
        *out++ = *in++;
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t size) {
    u8 *out = (u8 *)destination;
    const u8 *in = (const u8 *)source;
    if (out < in) {
        return memcpy(destination, source, size);
    }
    while (size-- != 0) {
        out[size] = in[size];
    }
    return destination;
}

void *memset(void *destination, int value, size_t size) {
    u8 *out = (u8 *)destination;
    while (size-- != 0) {
        *out++ = (u8)value;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t size) {
    const u8 *a = (const u8 *)left;
    const u8 *b = (const u8 *)right;
    while (size-- != 0) {
        if (*a != *b) {
            return (int)*a - (int)*b;
        }
        ++a;
        ++b;
    }
    return 0;
}

u32 load_u32(const void *address) {
    u32 value;
    /* Signatures frequently expose unaligned x86 operands; avoid typed loads. */
    memcpy(&value, address, sizeof(value));
    return value;
}

size_t ascii_length(const char *text) {
    size_t length = 0;
    if (text != NULL) {
        while (text[length] != '\0') {
            ++length;
        }
    }
    return length;
}

size_t wide_length(const wchar_t *text) {
    size_t length = 0;
    if (text != NULL) {
        while (text[length] != L'\0') {
            ++length;
        }
    }
    return length;
}

BOOL wide_copy(wchar_t *destination, size_t capacity, const wchar_t *source) {
    size_t i = 0;
    if (destination == NULL || source == NULL || capacity == 0) {
        return FALSE;
    }
    while (source[i] != L'\0') {
        if (i + 1 >= capacity) {
            /* Callers can safely treat failure as an empty path. */
            destination[0] = L'\0';
            return FALSE;
        }
        destination[i] = source[i];
        ++i;
    }
    destination[i] = L'\0';
    return TRUE;
}

BOOL wide_append(wchar_t *destination, size_t capacity, const wchar_t *suffix) {
    size_t length = wide_length(destination);
    size_t i = 0;
    if (destination == NULL || suffix == NULL || length >= capacity) {
        return FALSE;
    }
    while (suffix[i] != L'\0') {
        if (length + i + 1 >= capacity) {
            return FALSE;
        }
        destination[length + i] = suffix[i];
        ++i;
    }
    destination[length + i] = L'\0';
    return TRUE;
}

BOOL path_replace_filename(wchar_t *path, size_t capacity, const wchar_t *filename) {
    size_t length = wide_length(path);
    /* GetModuleFileNameW returns the DLL path; strip only its final component. */
    while (length != 0 && path[length - 1] != L'\\' && path[length - 1] != L'/') {
        --length;
    }
    path[length] = L'\0';
    return wide_append(path, capacity, filename);
}
