#ifndef KW_FPS_PATCH_FRAME_PACER_H
#define KW_FPS_PATCH_FRAME_PACER_H

#include "kw_common.h"

BOOL kw_frame_pacer_initialize(BOOL enabled);
kw_u8 *kw_frame_pacer_stub(void);
void kw_frame_pacer_reset(void);

#endif
