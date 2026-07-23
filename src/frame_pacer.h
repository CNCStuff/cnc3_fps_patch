#ifndef FPS_PATCH_FRAME_PACER_H
#define FPS_PATCH_FRAME_PACER_H

#include "common.h"
#include "config.h"
#include "game_layout.h"

BOOL frame_pacer_initialize(
    const GameLayout *game, const Config *config, BOOL enabled);
/* Entry address of the generated x86 branch island used by game_patches. */
u8 *frame_pacer_stub(void);
/* Rebase the next deadline after startup or a new game session. */
void frame_pacer_reset(void);

#endif
