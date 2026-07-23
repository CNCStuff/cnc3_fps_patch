#include "log.h"

static HANDLE g_log_file = INVALID_HANDLE_VALUE;
static BOOL g_log_enabled;

static void write_raw(const char *data, size_t size) {
    DWORD written;
    if (!g_log_enabled || g_log_file == INVALID_HANDLE_VALUE || data == NULL || size == 0) return;
    /* Logging is diagnostic-only; a failed write must never disable the patch. */
    WriteFile(g_log_file, data, (DWORD)size, &written, NULL);
}

/* Decimal formatting is local because the DLL deliberately has no CRT. */
static size_t format_u32(char *buffer, size_t capacity, u32 value) {
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

BOOL log_open(const wchar_t *path, BOOL enabled) {
    g_log_enabled = enabled;
    if (!enabled) return TRUE;
    /* FILE_SHARE_READ allows inspecting the bootstrap log while the game runs. */
    g_log_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    return g_log_file != INVALID_HANDLE_VALUE;
}

void log_close(void) {
    if (g_log_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_log_file);
        g_log_file = INVALID_HANDLE_VALUE;
    }
}

void log_text(const char *text) {
    write_raw(text, ascii_length(text));
}

void log_line(const char *text) {
    log_text(text);
    write_raw("\r\n", 2);
}

void log_u32(const char *label, u32 value) {
    char number[16];
    log_text(label);
    format_u32(number, sizeof(number), value);
    log_line(number);
}

void log_hex32(const char *label, u32 value) {
    static const char digits[] = "0123456789ABCDEF";
    char text[11];
    int i;
    text[0] = '0';
    text[1] = 'x';
    for (i = 0; i < 8; ++i) text[2 + i] = digits[(value >> ((7 - i) * 4)) & 0xFu];
    text[10] = '\0';
    log_text(label);
    log_line(text);
}
