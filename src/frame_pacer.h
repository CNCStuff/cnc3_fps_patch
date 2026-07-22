#ifndef KW_FPS_PATCH_FRAME_PACER_H
#define KW_FPS_PATCH_FRAME_PACER_H

#include "kw_common.h"
#include "config.h"
#include "game_layout.h"

BOOL kw_frame_pacer_initialize(
    const KwGameLayout *game, const KwConfig *config, BOOL enabled);
kw_u8 *kw_frame_pacer_stub(void);
void kw_frame_pacer_reset(void);

#endif
