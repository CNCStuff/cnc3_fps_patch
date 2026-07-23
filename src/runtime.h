#ifndef KW_FPS_PATCH_RUNTIME_H
#define KW_FPS_PATCH_RUNTIME_H

#include "kw_common.h"

typedef enum KwBootstrapStatus {
    KW_BOOTSTRAP_NOT_ATTEMPTED = 0,
    KW_BOOTSTRAP_INSTALLED = 1,
    KW_BOOTSTRAP_BAD_PE = 2,
    KW_BOOTSTRAP_BAD_PATCH_SITES = 3,
    KW_BOOTSTRAP_WRITE_FAILED = 4
} KwBootstrapStatus;

BOOL kw_runtime_attach(HMODULE self_module, kw_u8 *game_module);
void kw_runtime_proxy_checkpoint(void);
int KW_THISCALL kw_runtime_config_tail_hook(void *original_this);
int kw_runtime_config_noarg_hook(void);
int kw_start_session_tail_hook(void);

#endif
