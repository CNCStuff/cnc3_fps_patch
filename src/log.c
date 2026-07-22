#include "log.h"

static HANDLE g_log_file = INVALID_HANDLE_VALUE;
static BOOL g_log_enabled;

static void kw_write_raw(const char *data, size_t size) {
    DWORD written;
    if (!g_log_enabled || g_log_file == INVALID_HANDLE_VALUE || data == NULL || size == 0) return;
    WriteFile(g_log_file, data, (DWORD)size, &written, NULL);
}

static size_t kw_format_u32(char *buffer, size_t capacity, kw_u32 value) {
    char reverse[16];
    size_t count = 0;
    size_t i;
    do {
        reverse[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0 && count < sizeof(reverse));
    if (count >= capacity) return 0;
    for (i = 0; i < count; ++i) buffer[i] = reverse[count - 1u - i];
    buffer[count] = '\0';
    return count;
}

BOOL kw_log_open(const wchar_t *path, BOOL enabled) {
    g_log_enabled = enabled;
    if (!enabled) return TRUE;
    g_log_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    return g_log_file != INVALID_HANDLE_VALUE;
}

void kw_log_close(void) {
    if (g_log_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_log_file);
        g_log_file = INVALID_HANDLE_VALUE;
    }
}

void kw_log_text(const char *text) {
    kw_write_raw(text, kw_ascii_length(text));
}

void kw_log_line(const char *text) {
    kw_log_text(text);
    kw_write_raw("\r\n", 2);
}

void kw_log_u32(const char *label, kw_u32 value) {
    char number[16];
    kw_log_text(label);
    kw_format_u32(number, sizeof(number), value);
    kw_log_line(number);
}

void kw_log_hex32(const char *label, kw_u32 value) {
    static const char digits[] = "0123456789ABCDEF";
    char text[11];
    int i;
    text[0] = '0'; text[1] = 'x';
    for (i = 0; i < 8; ++i) text[2 + i] = digits[(value >> ((7 - i) * 4)) & 0xFu];
    text[10] = '\0';
    kw_log_text(label);
    kw_log_line(text);
}
