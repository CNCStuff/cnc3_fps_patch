#ifndef FPS_PATCH_GAME_LAYOUT_H
#define FPS_PATCH_GAME_LAYOUT_H

#include "common.h"

typedef struct BranchSite {
    /* RVAs of the five-byte CALL/JMP and its decoded in-image destination. */
    u32 instruction;
    u32 target;
} BranchSite;

typedef enum RuntimeConfigHookKind {
    RUNTIME_CONFIG_HOOK_THISCALL,
    RUNTIME_CONFIG_HOOK_NOARG
} RuntimeConfigHookKind;

typedef struct BootstrapLayout {
    /* Late call whose ABI-specific wrapper performs first-time installation. */
    BranchSite runtime_config_call;
    /* Session-start tail call used to refresh mutable engine fields. */
    BranchSite start_session_tail;
    RuntimeConfigHookKind runtime_config_hook_kind;
} BootstrapLayout;

typedef struct TimingLayout {
    /* All fields are RVAs of globals or globals containing runtime pointers. */
    u32 logic_fps;
    u32 client_fps;
    u32 game_engine_pointer;
    u32 global_data_pointer;
    u32 w3d_milliseconds_per_frame;
    u32 w3d_accumulated_time_ms;
    u32 client_fps_float;
    u32 audio_milliseconds_per_frame;
    u32 visual_seconds_per_frame;
} TimingLayout;

typedef struct SchedulerLayout {
    /* Start of the 22-byte integer batching expression in DispatchLogicPhase. */
    u32 phase_batch_block;
    /* Entry of the 48-byte __thiscall interpolation leaf function. */
    u32 phase_interpolation_function;
    /* Original sub_6DE5B1 call at byte 34 of the 39-byte phase-end block. */
    BranchSite client_slice_flush;
} SchedulerLayout;

typedef struct W3DClockLayout {
    /* Blocks immediately before the preserved W3D_SetCurrentTimeMs calls. */
    u32 special_advance_block;
    u32 normal_advance_block;
} W3DClockLayout;

typedef struct VisualLayout {
    /* Shared retail 1/30 scalar and selected instruction operands that read it. */
    u32 retail_step;
    u32 camera_step_operand;
    u32 laser_step_operand;
    u32 model_step_operand;

    u32 tracer_reset_frame_call;
    u32 tracer_update_frame_call;
    u32 cloud_frame_call;
    u32 anim2d_timestamp_frame_call;
    u32 anim2d_update_frame_call;
    BranchSite particle_simulation;

    u32 gpu_particle_fps_instruction;
    u32 gpu_particle_fps_operand;
    u32 radius_cursor_fps_instruction;
    u32 radius_cursor_fps_operand;
    u32 retail_frames_per_millisecond;
} VisualLayout;

typedef struct PacingLayout {
    /* Stock 29 ms presentation wait and GameEngine main-loop limiter sites. */
    u32 display_limiter_branch;
    u32 outer_gate;
    u32 no_limit_path;
    u32 history_path;
    u32 network_scale;
    u32 enforce_limit_flag;
    u32 total_wait_ms;
    u32 last_wait_ms;
    u32 last_frame_duration_ms;
    u32 previous_frame_time_ms;
    u32 milliseconds_per_logic_frame;
} PacingLayout;

/*
 * Every address is an RVA resolved from the loaded executable. Grouping sites
 * by subsystem keeps the runtime code explicit without mirroring every field
 * through a second layer of preprocessor aliases.
 */
typedef struct GameLayout {
    u8 *module;
    const char *target_name;
    u32 pe_timestamp;
    u32 pe_entry_rva;
    u32 pe_size_of_image;
    BootstrapLayout bootstrap;
    TimingLayout timing;
    SchedulerLayout scheduler;
    W3DClockLayout w3d_clock;
    VisualLayout visual;
    PacingLayout pacing;
} GameLayout;

typedef enum GameResolveResult {
    GAME_RESOLVED,
    GAME_INVALID_PE,
    GAME_UNSUPPORTED_BUILD
} GameResolveResult;

enum {
    /* GameEngine offsets recovered from KW 1.02 RTTI-backed class analysis. */
    ENGINE_MAX_UPDATE_FPS = 0x18,
    ENGINE_CURRENT_LOGIC_PHASE = 0x40,
    ENGINE_NOMINAL_CLIENT_FRAMES_PER_LOGIC_TICK = 0x44,
    ENGINE_LOGIC_PHASE_INTERPOLATION = 0x48,
    ENGINE_FRAME_DURATION_HISTORY_MS = 0x5C,
    ENGINE_FRAME_DURATION_HISTORY_COUNT = 64,
    ENGINE_FRAME_DURATION_HISTORY_SUM_MS = 0x15C,
    ENGINE_FRAME_DURATION_HISTORY_INDEX = 0x160,
    ENGINE_PACING_UPDATE_MULTIPLIER = 0x164,

    /* GlobalData fields reached through the resolved g_theWriteableGlobalData pointer. */
    GLOBAL_DATA_USE_FPS_LIMIT = 0x52,
    GLOBAL_DATA_FPS_LIMIT = 0x54
};

static inline u8 *game_address(const GameLayout *game, u32 rva) {
    return game->module + rva;
}

GameResolveResult resolve_game_layout(GameLayout *out_game, u8 *module);

#endif
