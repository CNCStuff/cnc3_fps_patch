#ifndef FPS_PATCH_FRAME_PACER_H
#define FPS_PATCH_FRAME_PACER_H

#include "common.h"
#include "config.h"
#include "game_layout.h"

BOOL frame_pacer_initialize(
    const GameLayout *game, const Config *config, BOOL enabled);
u8 *frame_pacer_stub(void);
void frame_pacer_reset(void);

#endif
