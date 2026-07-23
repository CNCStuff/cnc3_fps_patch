#include "runtime.h"

#include "config.h"
#include "frame_pacer.h"
#include "game_layout.h"
#include "game_patches.h"
#include "log.h"
#include "memory_patch.h"

typedef struct Runtime {
    HMODULE self_module;
    Config config;
    GameLayout game;
    volatile LONG bootstrap_status;
    volatile LONG file_init_state;
    volatile LONG proxy_checkpoint_logged;
    BOOL disabled;
    wchar_t ini_path[MAX_PATH];
    wchar_t log_path[MAX_PATH];
} Runtime;

typedef int(THISCALL *RuntimeConfigThiscallFn)(void *original_this);
typedef int (*RuntimeConfigNoArgFn)(void);
typedef int (*StartSessionTailFn)(void);

/* One owner for configuration, resolved addresses, and lifecycle state. */
static Runtime g_runtime;

static BOOL initialize_runtime_files(Runtime *runtime) {
    LONG state = InterlockedCompareExchange(&runtime->file_init_state, 1, 0);
    BOOL ini_found;

    if (state == 2) return !runtime->disabled;
    if (state == 1) {
        while (InterlockedCompareExchange(&runtime->file_init_state, 1, 1) == 1) Sleep(0);
        return runtime->file_init_state == 2 && !runtime->disabled;
    }

    /* This runs from normal game/proxy execution, never under the loader lock. */
    config_set_defaults(&runtime->config);
    if (GetModuleFileNameW(runtime->self_module, runtime->ini_path,
                           ARRAY_COUNT(runtime->ini_path)) == 0 ||
        !path_replace_filename(runtime->ini_path, ARRAY_COUNT(runtime->ini_path),
                               L"fps_patch.ini") ||
        !wide_copy(runtime->log_path, ARRAY_COUNT(runtime->log_path),
                   runtime->ini_path) ||
        !path_replace_filename(runtime->log_path, ARRAY_COUNT(runtime->log_path),
                               L"fps_patch.log")) {
        runtime->disabled = TRUE;
        InterlockedExchange(&runtime->file_init_state, 2);
        return FALSE;
    }

    ini_found = config_load(&runtime->config, runtime->ini_path);
    log_open(runtime->log_path, runtime->config.logging);
    log_line("fps_patch bootstrap");
    log_text("Resolved target: ");
    log_line(runtime->game.target_name != NULL
                 ? runtime->game.target_name
                 : "unresolved executable");
    log_hex32("PE timestamp=", runtime->game.pe_timestamp);
    log_hex32("PE image size=", runtime->game.pe_size_of_image);
    log_line(ini_found ? "Configuration: fps_patch.ini loaded"
                       : "Configuration: using built-in defaults");
    log_u32("target_fps=", runtime->config.target_fps);
    log_u32("precise_pacing=", runtime->config.precise_pacing != 0);
    log_u32("bootstrap_status=", (u32)runtime->bootstrap_status);

    if (!runtime->config.enabled) {
        log_line("Patch disabled by configuration");
        runtime->disabled = TRUE;
    } else if (runtime->bootstrap_status != BOOTSTRAP_INSTALLED) {
        log_line("ERROR: bootstrap callsite hooks were not installed");
        runtime->disabled = TRUE;
    } else if (!frame_pacer_initialize(&runtime->game, &runtime->config,
                                       runtime->config.precise_pacing)) {
        log_line("ERROR: could not initialize the precise frame pacer");
        runtime->disabled = TRUE;
    }

    InterlockedExchange(&runtime->file_init_state, 2);
    return !runtime->disabled;
}

static BOOL apply_fixed_rate(Runtime *runtime) {
    const TimingLayout *timing = &runtime->game.timing;
    const PacingLayout *pacing = &runtime->game.pacing;
    const u32 target_fps = runtime->config.target_fps;
    const u32 ratio = target_fps / 15u;
    const u32 seed_ms = 1000u / target_fps;
    u8 *engine = *(u8 **)game_address(&runtime->game,
                                      timing->game_engine_pointer);
    u8 *global_data = *(u8 **)game_address(&runtime->game,
                                           timing->global_data_pointer);
    u32 i;

    if (engine == NULL || global_data == NULL) {
        log_line("ERROR: live GameEngine or GlobalData pointer is null");
        return FALSE;
    }

    *(u32 *)game_address(&runtime->game, timing->client_fps) = target_fps;
    /*
     * Startup cached three representations of the client rate. They have
     * different units and must all track the raised rate:
     *
     *   float(client FPS)
     *   1000 / FPS  -- audio milliseconds per client frame
     *   1 / FPS     -- visual seconds per client frame
     *
     * The nearby 15 Hz logic constants and the shaders' separate 30-frame
     * particle time domain intentionally remain unchanged.
     */
    *(float *)game_address(&runtime->game, timing->client_fps_float) =
        (float)target_fps;
    *(float *)game_address(&runtime->game, timing->audio_milliseconds_per_frame) =
        1000.0f / (float)target_fps;
    *(float *)game_address(&runtime->game, timing->visual_seconds_per_frame) =
        1.0f / (float)target_fps;
    *(u32 *)game_address(&runtime->game, timing->w3d_milliseconds_per_frame) = seed_ms;

    global_data[GLOBAL_DATA_USE_FPS_LIMIT] = 1;
    *(u32 *)(global_data + GLOBAL_DATA_FPS_LIMIT) = target_fps;
    *(u32 *)(engine + ENGINE_MAX_UPDATE_FPS) = target_fps;
    *(u32 *)(engine + ENGINE_NOMINAL_CLIENT_FRAMES_PER_LOGIC_TICK) = ratio;
    *(u32 *)(engine + ENGINE_PACING_UPDATE_MULTIPLIER) = ratio;
    for (i = 0; i < ENGINE_FRAME_DURATION_HISTORY_COUNT; ++i) {
        *(u32 *)(engine + ENGINE_FRAME_DURATION_HISTORY_MS + i * 4u) = seed_ms;
    }
    *(u32 *)(engine + ENGINE_FRAME_DURATION_HISTORY_SUM_MS) =
        seed_ms * ENGINE_FRAME_DURATION_HISTORY_COUNT;
    *(u32 *)(engine + ENGINE_FRAME_DURATION_HISTORY_INDEX) = 0;
    *(u32 *)game_address(&runtime->game, pacing->previous_frame_time_ms) = timeGetTime();

    game_patches_reset_state();
    frame_pacer_reset();
    log_u32("Applied client FPS=", target_fps);
    log_u32("Applied W3D milliseconds/client-frame=", seed_ms);
    log_hex32("Applied audio milliseconds/client-frame bits=",
              load_u32(game_address(
                  &runtime->game, timing->audio_milliseconds_per_frame)));
    return TRUE;
}

static void apply_from_game_hook(Runtime *runtime, const char *source) {
    if (!initialize_runtime_files(runtime)) return;
    log_text("Game hook reached: ");
    log_line(source);

    if (!game_patches_install(&runtime->game, &runtime->config,
                              frame_pacer_stub())) {
        runtime->disabled = TRUE;
        log_line("ERROR: disabling patch after static installation failure");
    } else if (!apply_fixed_rate(runtime)) {
        runtime->disabled = TRUE;
        log_line("ERROR: disabling patch after live-state initialization failure");
    }
}

static BOOL install_bootstrap_hooks(Runtime *runtime) {
    const BootstrapLayout *bootstrap = &runtime->game.bootstrap;
    const void *runtime_hook;
    PatchTransaction transaction;
    u8 runtime_patch[5] = { 0xE8, 0, 0, 0, 0 };
    u8 session_patch[5] = { 0xE9, 0, 0, 0, 0 };

    runtime_hook = bootstrap->runtime_config_hook_kind == RUNTIME_CONFIG_HOOK_NOARG
                       ? (const void *)runtime_config_noarg_hook
                       : (const void *)runtime_config_thiscall_hook;
    encode_rel32(&runtime_patch[1],
                 game_address(&runtime->game,
                              bootstrap->runtime_config_call.instruction + 5u),
                 runtime_hook);
    encode_rel32(&session_patch[1],
                 game_address(&runtime->game,
                              bootstrap->start_session_tail.instruction + 5u),
                 start_session_tail_hook);
    patch_transaction_init(&transaction);
    if (!patch_transaction_write(
            &transaction,
            game_address(&runtime->game,
                         bootstrap->runtime_config_call.instruction),
            runtime_patch, sizeof(runtime_patch)) ||
        !patch_transaction_write(
            &transaction,
            game_address(&runtime->game,
                         bootstrap->start_session_tail.instruction),
            session_patch, sizeof(session_patch))) {
        patch_transaction_rollback(&transaction);
        runtime->bootstrap_status = BOOTSTRAP_WRITE_FAILED;
        return FALSE;
    }
    runtime->bootstrap_status = BOOTSTRAP_INSTALLED;
    return TRUE;
}

BOOL runtime_attach(HMODULE self_module, u8 *game_module) {
    GameResolveResult result;

    g_runtime.self_module = self_module;
    result = resolve_game_layout(&g_runtime.game, game_module);
    if (result == GAME_INVALID_PE) {
        g_runtime.bootstrap_status = BOOTSTRAP_BAD_PE;
        return FALSE;
    }
    if (result != GAME_RESOLVED) {
        g_runtime.bootstrap_status = BOOTSTRAP_BAD_PATCH_SITES;
        return FALSE;
    }
    return install_bootstrap_hooks(&g_runtime);
}

void runtime_proxy_checkpoint(void) {
    initialize_runtime_files(&g_runtime);
    if (InterlockedCompareExchange(&g_runtime.proxy_checkpoint_logged, 1, 0) == 0) {
        log_line("dinput8 proxy forwarding initialized");
    }
}

int THISCALL runtime_config_thiscall_hook(void *original_this) {
    RuntimeConfigThiscallFn original = (RuntimeConfigThiscallFn)game_address(
        &g_runtime.game, g_runtime.game.bootstrap.runtime_config_call.target);
    int result = original(original_this);
    apply_from_game_hook(&g_runtime, "GameEngine runtime configuration trigger (__thiscall)");
    return result;
}

int runtime_config_noarg_hook(void) {
    RuntimeConfigNoArgFn original = (RuntimeConfigNoArgFn)game_address(
        &g_runtime.game, g_runtime.game.bootstrap.runtime_config_call.target);
    int result = original();
    apply_from_game_hook(&g_runtime, "GameEngine runtime configuration trigger (no-arg)");
    return result;
}

int start_session_tail_hook(void) {
    StartSessionTailFn original = (StartSessionTailFn)game_address(
        &g_runtime.game, g_runtime.game.bootstrap.start_session_tail.target);
    int result = original();
    apply_from_game_hook(&g_runtime, "GameEngine_StartGameSession tail");
    return result;
}
