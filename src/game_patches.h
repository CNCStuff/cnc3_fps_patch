#ifndef KW_FPS_PATCH_GAME_PATCHES_H
#define KW_FPS_PATCH_GAME_PATCHES_H

#include "kw_common.h"
#include "config.h"
#include "game_layout.h"

BOOL kw_game_patches_install(const KwGameLayout *game, const KwConfig *config,
                             kw_u8 *pacing_stub);
void kw_game_patches_reset_state(void);

#endif
