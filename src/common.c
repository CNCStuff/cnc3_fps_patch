#include "common.h"

u32 load_u32(const void *address) {
    u32 value;
    /* Signatures frequently expose unaligned x86 operands; avoid typed loads. */
    memcpy(&value, address, sizeof(value));
    return value;
}

BOOL wide_copy(wchar_t *destination, size_t capacity, const wchar_t *source) {
    size_t length;
    if (destination == NULL || source == NULL || capacity == 0) {
        return FALSE;
    }
    length = wcslen(source);
    if (length >= capacity) {
        /* Callers can safely treat failure as an empty path. */
        destination[0] = L'\0';
        return FALSE;
    }
    wmemcpy(destination, source, length + 1u);
    return TRUE;
}

BOOL wide_append(wchar_t *destination, size_t capacity, const wchar_t *suffix) {
    size_t length;
    size_t suffix_length;
    if (destination == NULL || suffix == NULL || capacity == 0) {
        return FALSE;
    }
    length = wcslen(destination);
    suffix_length = wcslen(suffix);
    if (length >= capacity || suffix_length >= capacity - length) return FALSE;
    wmemcpy(destination + length, suffix, suffix_length + 1u);
    return TRUE;
}

BOOL path_replace_filename(wchar_t *path, size_t capacity, const wchar_t *filename) {
    size_t length;
    if (path == NULL || filename == NULL || capacity == 0) return FALSE;
    length = wcslen(path);
    /* GetModuleFileNameW returns the DLL path; strip only its final component. */
    while (length != 0 && path[length - 1] != L'\\' && path[length - 1] != L'/') {
        --length;
    }
    path[length] = L'\0';
    return wide_append(path, capacity, filename);
}
