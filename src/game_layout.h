#ifndef KW_FPS_PATCH_GAME_LAYOUT_H
#define KW_FPS_PATCH_GAME_LAYOUT_H

#include "kw_common.h"

typedef struct KwBranchSite {
    kw_u32 instruction;
    kw_u32 target;
} KwBranchSite;

typedef struct KwBootstrapLayout {
    KwBranchSite runtime_config_tail;
    KwBranchSite start_session_tail;
} KwBootstrapLayout;

typedef struct KwTimingLayout {
    kw_u32 logic_fps;
    kw_u32 client_fps;
    kw_u32 game_engine_pointer;
    kw_u32 global_data_pointer;
    kw_u32 w3d_milliseconds_per_frame;
    kw_u32 client_fps_float;
    kw_u32 audio_milliseconds_per_frame;
    kw_u32 visual_seconds_per_frame;
} KwTimingLayout;

typedef struct KwVisualLayout {
    kw_u32 retail_step;
    kw_u32 camera_step_operand;
    kw_u32 laser_step_operand;
    kw_u32 model_step_operand;

    kw_u32 tracer_reset_frame_call;
    kw_u32 tracer_update_frame_call;
    kw_u32 cloud_frame_call;
    kw_u32 anim2d_timestamp_frame_call;
    kw_u32 anim2d_update_frame_call;
    KwBranchSite particle_simulation;

    kw_u32 gpu_particle_fps_instruction;
    kw_u32 gpu_particle_fps_operand;
    kw_u32 radius_cursor_fps_instruction;
    kw_u32 radius_cursor_fps_operand;
    kw_u32 retail_frames_per_millisecond;
} KwVisualLayout;

typedef struct KwPacingLayout {
    kw_u32 display_limiter_branch;
    kw_u32 outer_gate;
    kw_u32 no_limit_path;
    kw_u32 history_path;
    kw_u32 network_scale;
    kw_u32 enforce_limit_flag;
    kw_u32 total_wait_ms;
    kw_u32 last_wait_ms;
    kw_u32 last_frame_duration_ms;
    kw_u32 previous_frame_time_ms;
    kw_u32 milliseconds_per_logic_frame;
} KwPacingLayout;

/*
 * Every address is an RVA resolved from the loaded executable. Grouping sites
 * by subsystem keeps the runtime code explicit without mirroring every field
 * through a second layer of preprocessor aliases.
 */
typedef struct KwGameLayout {
    kw_u8 *module;
    const char *build_name;
    kw_u32 pe_timestamp;
    kw_u32 pe_entry_rva;
    kw_u32 pe_size_of_image;
    KwBootstrapLayout bootstrap;
    KwTimingLayout timing;
    KwVisualLayout visual;
    KwPacingLayout pacing;
} KwGameLayout;

typedef enum KwGameResolveResult {
    KW_GAME_RESOLVED,
    KW_GAME_INVALID_PE,
    KW_GAME_UNSUPPORTED_BUILD
} KwGameResolveResult;

enum {
    KW_ENGINE_MAX_UPDATE_FPS = 0x18,
    KW_ENGINE_NOMINAL_CLIENT_FRAMES_PER_LOGIC_TICK = 0x44,
    KW_ENGINE_FRAME_DURATION_HISTORY_MS = 0x5C,
    KW_ENGINE_FRAME_DURATION_HISTORY_COUNT = 64,
    KW_ENGINE_FRAME_DURATION_HISTORY_SUM_MS = 0x15C,
    KW_ENGINE_FRAME_DURATION_HISTORY_INDEX = 0x160,
    KW_ENGINE_PACING_UPDATE_MULTIPLIER = 0x164,

    KW_GLOBAL_DATA_USE_FPS_LIMIT = 0x52,
    KW_GLOBAL_DATA_FPS_LIMIT = 0x54
};

static inline kw_u8 *kw_game_address(const KwGameLayout *game, kw_u32 rva) {
    return game->module + rva;
}

KwGameResolveResult kw_resolve_game_layout(KwGameLayout *out_game, kw_u8 *module);

#endif
