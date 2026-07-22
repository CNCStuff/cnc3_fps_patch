#ifndef KW_FPS_PATCH_CONFIG_H
#define KW_FPS_PATCH_CONFIG_H

#include "kw_common.h"

void kw_config_set_defaults(KwConfig *config);
BOOL kw_config_load(KwConfig *config, const wchar_t *path);

#endif
