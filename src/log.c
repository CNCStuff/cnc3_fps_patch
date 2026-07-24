#include "log.h"

#include <stdio.h>

static HANDLE g_log_file = INVALID_HANDLE_VALUE;
static BOOL g_log_enabled;

static void write_raw(const char *data, size_t size) {
    DWORD written;
    if (!g_log_enabled || g_log_file == INVALID_HANDLE_VALUE || data == NULL || size == 0) return;
    /* Logging is diagnostic-only; a failed write must never disable the patch. */
    WriteFile(g_log_file, data, (DWORD)size, &written, NULL);
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
    if (text != NULL) write_raw(text, strlen(text));
}

void log_line(const char *text) {
    log_text(text);
    write_raw("\r\n", 2);
}

void log_u32(const char *label, u32 value) {
    char number[16];
    log_text(label);
    snprintf(number, sizeof(number), "%u", (unsigned int)value);
    log_line(number);
}

void log_hex32(const char *label, u32 value) {
    char text[11];
    snprintf(text, sizeof(text), "0x%08X", (unsigned int)value);
    log_text(label);
    log_line(text);
}
