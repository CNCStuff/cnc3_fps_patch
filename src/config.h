#ifndef KW_FPS_PATCH_CONFIG_H
#define KW_FPS_PATCH_CONFIG_H

#include "kw_common.h"

typedef struct KwConfig {
    BOOL enabled;
    kw_u32 target_fps;
    BOOL precise_pacing;
    kw_u32 spin_threshold_us;
    BOOL logging;
} KwConfig;

void kw_config_set_defaults(KwConfig *config);
BOOL kw_config_load(KwConfig *config, const wchar_t *path);

#endif
