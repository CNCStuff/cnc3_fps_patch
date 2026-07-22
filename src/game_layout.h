#ifndef KW_FPS_PATCH_GAME_LAYOUT_H
#define KW_FPS_PATCH_GAME_LAYOUT_H

#include "kw_common.h"

/*
 * Resolved Kane's Wrath engine locations.  Code sites are found with unique
 * masked signatures; absolute operands and relative branches are then decoded
 * from those instructions.  Keeping the result as RVAs preserves ASLR safety
 * without tying the patch to one linker layout.
 */
typedef struct KwGameLayout {
    const char *build_name;
    kw_u32 pe_timestamp;
    kw_u32 pe_entry_rva;
    kw_u32 pe_size_of_image;

    kw_u32 runtime_config_tail_call;
    kw_u32 runtime_config_tail_target;
    kw_u32 start_session_tail_jump;
    kw_u32 start_session_tail_target;

    kw_u32 logic_fps;
    kw_u32 client_update_fps;
    kw_u32 game_engine_pointer;
    kw_u32 global_data_pointer;
    kw_u32 w3d_milliseconds_per_client_frame;
    kw_u32 client_fps_float;
    kw_u32 audio_milliseconds_per_client_frame;
    kw_u32 visual_seconds_per_client_frame;

    kw_u32 visual_step_camera_operand;
    kw_u32 visual_step_laser_operand;
    kw_u32 visual_step_model_operand;
    kw_u32 retail_visual_step;

    kw_u32 tracer_reset_get_frame;
    kw_u32 tracer_update_get_frame;
    kw_u32 cloud_effect_get_frame;
    kw_u32 anim2d_set_frame_get_frame;
    kw_u32 anim2d_update_get_frame;
    kw_u32 fx_particle_simulation_call;
    kw_u32 fx_particle_simulation_target;
    kw_u32 gpu_particle_frame_rate_instruction;
    kw_u32 gpu_particle_frame_rate_operand;
    kw_u32 radius_cursor_throb_frame_rate_instruction;
    kw_u32 radius_cursor_throb_frame_rate_operand;
    kw_u32 legacy_visual_frames_per_millisecond;

    kw_u32 display_limiter_branch;
    kw_u32 outer_pacing_gate;
    kw_u32 outer_pacing_no_limit;
    kw_u32 outer_pacing_history;
    kw_u32 network_frame_pacing_scale;
    kw_u32 enforce_fps_limit_this_frame;
    kw_u32 total_limiter_wait_ms;
    kw_u32 last_limiter_wait_ms;
    kw_u32 last_engine_frame_duration_ms;
    kw_u32 previous_engine_frame_time_ms;
    kw_u32 milliseconds_per_logic_frame;
} KwGameLayout;

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

extern KwGameLayout g_kw_game_layout;

/* Runtime-facing aliases keep patch code readable while the values are now
 * populated by the signature resolver rather than compile-time constants. */
#define KW_RVA_RUNTIME_CONFIG_TAIL_CALL (g_kw_game_layout.runtime_config_tail_call)
#define KW_RVA_RUNTIME_CONFIG_TAIL_TARGET (g_kw_game_layout.runtime_config_tail_target)
#define KW_RVA_START_SESSION_TAIL_JUMP (g_kw_game_layout.start_session_tail_jump)
#define KW_RVA_START_SESSION_TAIL_TARGET (g_kw_game_layout.start_session_tail_target)
#define KW_RVA_LOGIC_FPS (g_kw_game_layout.logic_fps)
#define KW_RVA_CLIENT_UPDATE_FPS (g_kw_game_layout.client_update_fps)
#define KW_RVA_GAME_ENGINE_POINTER (g_kw_game_layout.game_engine_pointer)
#define KW_RVA_GLOBAL_DATA_POINTER (g_kw_game_layout.global_data_pointer)
#define KW_RVA_W3D_MILLISECONDS_PER_CLIENT_FRAME \
    (g_kw_game_layout.w3d_milliseconds_per_client_frame)
#define KW_RVA_CLIENT_FPS_FLOAT (g_kw_game_layout.client_fps_float)
#define KW_RVA_AUDIO_MILLISECONDS_PER_CLIENT_FRAME \
    (g_kw_game_layout.audio_milliseconds_per_client_frame)
#define KW_RVA_VISUAL_SECONDS_PER_CLIENT_FRAME \
    (g_kw_game_layout.visual_seconds_per_client_frame)
#define KW_RVA_VISUAL_STEP_CAMERA_OPERAND (g_kw_game_layout.visual_step_camera_operand)
#define KW_RVA_VISUAL_STEP_LASER_OPERAND (g_kw_game_layout.visual_step_laser_operand)
#define KW_RVA_VISUAL_STEP_MODEL_OPERAND (g_kw_game_layout.visual_step_model_operand)
#define KW_RVA_RETAIL_VISUAL_STEP (g_kw_game_layout.retail_visual_step)
#define KW_RVA_TRACER_RESET_GET_FRAME (g_kw_game_layout.tracer_reset_get_frame)
#define KW_RVA_TRACER_UPDATE_GET_FRAME (g_kw_game_layout.tracer_update_get_frame)
#define KW_RVA_CLOUD_EFFECT_GET_FRAME (g_kw_game_layout.cloud_effect_get_frame)
#define KW_RVA_ANIM2D_SET_FRAME_GET_FRAME (g_kw_game_layout.anim2d_set_frame_get_frame)
#define KW_RVA_ANIM2D_UPDATE_GET_FRAME (g_kw_game_layout.anim2d_update_get_frame)
#define KW_RVA_FX_PARTICLE_SIMULATION_CALL (g_kw_game_layout.fx_particle_simulation_call)
#define KW_RVA_FX_PARTICLE_SIMULATION_TARGET (g_kw_game_layout.fx_particle_simulation_target)
#define KW_RVA_GPU_PARTICLE_FRAME_RATE_INSTRUCTION \
    (g_kw_game_layout.gpu_particle_frame_rate_instruction)
#define KW_RVA_GPU_PARTICLE_FRAME_RATE_OPERAND \
    (g_kw_game_layout.gpu_particle_frame_rate_operand)
#define KW_RVA_RADIUS_CURSOR_THROB_FRAME_RATE_INSTRUCTION \
    (g_kw_game_layout.radius_cursor_throb_frame_rate_instruction)
#define KW_RVA_RADIUS_CURSOR_THROB_FRAME_RATE_OPERAND \
    (g_kw_game_layout.radius_cursor_throb_frame_rate_operand)
#define KW_RVA_LEGACY_VISUAL_FRAMES_PER_MILLISECOND \
    (g_kw_game_layout.legacy_visual_frames_per_millisecond)
#define KW_RVA_DISPLAY_LIMITER_BRANCH (g_kw_game_layout.display_limiter_branch)
#define KW_RVA_OUTER_PACING_GATE (g_kw_game_layout.outer_pacing_gate)
#define KW_RVA_OUTER_PACING_NO_LIMIT (g_kw_game_layout.outer_pacing_no_limit)
#define KW_RVA_OUTER_PACING_HISTORY (g_kw_game_layout.outer_pacing_history)
#define KW_RVA_NETWORK_FRAME_PACING_SCALE (g_kw_game_layout.network_frame_pacing_scale)
#define KW_RVA_ENFORCE_FPS_LIMIT_THIS_FRAME \
    (g_kw_game_layout.enforce_fps_limit_this_frame)
#define KW_RVA_TOTAL_LIMITER_WAIT_MS (g_kw_game_layout.total_limiter_wait_ms)
#define KW_RVA_LAST_LIMITER_WAIT_MS (g_kw_game_layout.last_limiter_wait_ms)
#define KW_RVA_LAST_ENGINE_FRAME_DURATION_MS \
    (g_kw_game_layout.last_engine_frame_duration_ms)
#define KW_RVA_PREVIOUS_ENGINE_FRAME_TIME_MS \
    (g_kw_game_layout.previous_engine_frame_time_ms)
#define KW_RVA_MILLISECONDS_PER_LOGIC_FRAME \
    (g_kw_game_layout.milliseconds_per_logic_frame)

BOOL kw_validate_game_pe_headers(kw_u8 *module);
BOOL kw_resolve_game_layout(kw_u8 *module);

#endif
