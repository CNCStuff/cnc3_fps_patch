#ifndef FPS_PATCH_CONFIG_H
#define FPS_PATCH_CONFIG_H

#include "common.h"

typedef struct Config {
    BOOL enabled;
    u32 target_fps;
    BOOL precise_pacing;
    u32 spin_threshold_us;
    BOOL logging;
} Config;

void config_set_defaults(Config *config);
BOOL config_load(Config *config, const wchar_t *path);

#endif
