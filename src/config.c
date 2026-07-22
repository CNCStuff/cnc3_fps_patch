#include "config.h"

KwConfig g_kw_config;

void kw_config_set_defaults(KwConfig *config) {
    config->enabled = TRUE;
    config->target_fps = 90;
    config->precise_pacing = TRUE;
    config->spin_threshold_us = 400;
    config->logging = TRUE;
}

BOOL kw_config_load(KwConfig *config, const wchar_t *path) {
    DWORD attributes;
    int fps;
    if (config == NULL || path == NULL) return FALSE;
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return FALSE;
    }
    config->enabled = GetPrivateProfileIntW(L"kw_fps_patch", L"enabled", config->enabled, path) != 0;
    fps = (int)GetPrivateProfileIntW(L"kw_fps_patch", L"target_fps", config->target_fps, path);
    if (fps == 45 || fps == 90) config->target_fps = (kw_u32)fps;
    config->precise_pacing =
        GetPrivateProfileIntW(L"kw_fps_patch", L"precise_pacing", config->precise_pacing, path) != 0;
    config->spin_threshold_us = (kw_u32)GetPrivateProfileIntW(
        L"kw_fps_patch", L"spin_threshold_us", config->spin_threshold_us, path);
    if (config->spin_threshold_us > 5000u) config->spin_threshold_us = 5000u;
    config->logging = GetPrivateProfileIntW(L"kw_fps_patch", L"logging", config->logging, path) != 0;
    return TRUE;
}
