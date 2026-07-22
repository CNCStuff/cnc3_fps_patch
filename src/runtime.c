#include "runtime.h"

#include "config.h"
#include "frame_pacer.h"
#include "game_layout.h"
#include "game_patches.h"
#include "log.h"
#include "memory_patch.h"

typedef struct KwRuntime {
    HMODULE self_module;
    KwConfig config;
    KwGameLayout game;
    volatile LONG bootstrap_status;
    volatile LONG file_init_state;
    volatile LONG proxy_checkpoint_logged;
    BOOL disabled;
    wchar_t ini_path[MAX_PATH];
    wchar_t log_path[MAX_PATH];
} KwRuntime;

typedef int (KW_THISCALL *KwRuntimeConfigTailFn)(void *original_this);
typedef int (*KwStartSessionTailFn)(void);

/* One owner for configuration, resolved addresses, and lifecycle state. */
static KwRuntime g_runtime;

static BOOL kw_initialize_runtime_files(KwRuntime *runtime) {
    LONG state = InterlockedCompareExchange(&runtime->file_init_state, 1, 0);
    BOOL ini_found;

    if (state == 2) return !runtime->disabled;
    if (state == 1) {
        while (InterlockedCompareExchange(&runtime->file_init_state, 1, 1) == 1) Sleep(0);
        return runtime->file_init_state == 2 && !runtime->disabled;
    }

    /* This runs from normal game/proxy execution, never under the loader lock. */
    kw_config_set_defaults(&runtime->config);
    if (GetModuleFileNameW(runtime->self_module, runtime->ini_path,
                           KW_ARRAY_COUNT(runtime->ini_path)) == 0 ||
        !kw_path_replace_filename(runtime->ini_path, KW_ARRAY_COUNT(runtime->ini_path),
                                  L"kw_fps_patch.ini") ||
        !kw_wide_copy(runtime->log_path, KW_ARRAY_COUNT(runtime->log_path),
                      runtime->ini_path) ||
        !kw_path_replace_filename(runtime->log_path, KW_ARRAY_COUNT(runtime->log_path),
                                  L"kw_fps_patch.log")) {
        runtime->disabled = TRUE;
        InterlockedExchange(&runtime->file_init_state, 2);
        return FALSE;
    }

    ini_found = kw_config_load(&runtime->config, runtime->ini_path);
    kw_log_open(runtime->log_path, runtime->config.logging);
    kw_log_line("Kane's Wrath FPS patch bootstrap");
    kw_log_text("Resolved target: ");
    kw_log_line(runtime->game.build_name != NULL
                    ? runtime->game.build_name
                    : "unresolved executable");
    kw_log_hex32("PE timestamp=", runtime->game.pe_timestamp);
    kw_log_hex32("PE image size=", runtime->game.pe_size_of_image);
    kw_log_line(ini_found ? "Configuration: kw_fps_patch.ini loaded"
                          : "Configuration: using built-in defaults");
    kw_log_u32("target_fps=", runtime->config.target_fps);
    kw_log_u32("precise_pacing=", runtime->config.precise_pacing != 0);
    kw_log_u32("bootstrap_status=", (kw_u32)runtime->bootstrap_status);

    if (!runtime->config.enabled) {
        kw_log_line("Patch disabled by configuration");
        runtime->disabled = TRUE;
    } else if (runtime->bootstrap_status != KW_BOOTSTRAP_INSTALLED) {
        kw_log_line("ERROR: bootstrap callsite hooks were not installed");
        runtime->disabled = TRUE;
    } else if (!kw_frame_pacer_initialize(&runtime->game, &runtime->config,
                                          runtime->config.precise_pacing)) {
        kw_log_line("ERROR: could not initialize the precise frame pacer");
        runtime->disabled = TRUE;
    }

    InterlockedExchange(&runtime->file_init_state, 2);
    return !runtime->disabled;
}

static BOOL kw_apply_fixed_rate(KwRuntime *runtime) {
    const KwTimingLayout *timing = &runtime->game.timing;
    const KwPacingLayout *pacing = &runtime->game.pacing;
    const kw_u32 target_fps = runtime->config.target_fps;
    const kw_u32 ratio = target_fps / 15u;
    const kw_u32 seed_ms = 1000u / target_fps;
    kw_u8 *engine = *(kw_u8 **)kw_game_address(&runtime->game,
                                                timing->game_engine_pointer);
    kw_u8 *global_data = *(kw_u8 **)kw_game_address(&runtime->game,
                                                     timing->global_data_pointer);
    kw_u32 i;

    if (engine == NULL || global_data == NULL) {
        kw_log_line("ERROR: live GameEngine or GlobalData pointer is null");
        return FALSE;
    }

    *(kw_u32 *)kw_game_address(&runtime->game, timing->client_fps) = target_fps;
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
    *(float *)kw_game_address(&runtime->game, timing->client_fps_float) =
        (float)target_fps;
    *(float *)kw_game_address(&runtime->game, timing->audio_milliseconds_per_frame) =
        1000.0f / (float)target_fps;
    *(float *)kw_game_address(&runtime->game, timing->visual_seconds_per_frame) =
        1.0f / (float)target_fps;
    *(kw_u32 *)kw_game_address(&runtime->game, timing->w3d_milliseconds_per_frame) = seed_ms;

    global_data[KW_GLOBAL_DATA_USE_FPS_LIMIT] = 1;
    *(kw_u32 *)(global_data + KW_GLOBAL_DATA_FPS_LIMIT) = target_fps;
    *(kw_u32 *)(engine + KW_ENGINE_MAX_UPDATE_FPS) = target_fps;
    *(kw_u32 *)(engine + KW_ENGINE_NOMINAL_CLIENT_FRAMES_PER_LOGIC_TICK) = ratio;
    *(kw_u32 *)(engine + KW_ENGINE_PACING_UPDATE_MULTIPLIER) = ratio;
    for (i = 0; i < KW_ENGINE_FRAME_DURATION_HISTORY_COUNT; ++i) {
        *(kw_u32 *)(engine + KW_ENGINE_FRAME_DURATION_HISTORY_MS + i * 4u) = seed_ms;
    }
    *(kw_u32 *)(engine + KW_ENGINE_FRAME_DURATION_HISTORY_SUM_MS) =
        seed_ms * KW_ENGINE_FRAME_DURATION_HISTORY_COUNT;
    *(kw_u32 *)(engine + KW_ENGINE_FRAME_DURATION_HISTORY_INDEX) = 0;
    *(kw_u32 *)kw_game_address(&runtime->game, pacing->previous_frame_time_ms) = timeGetTime();

    kw_game_patches_reset_state();
    kw_frame_pacer_reset();
    kw_log_u32("Applied client FPS=", target_fps);
    kw_log_u32("Applied W3D milliseconds/client-frame=", seed_ms);
    kw_log_hex32("Applied audio milliseconds/client-frame bits=",
                 kw_load_u32(kw_game_address(
                     &runtime->game, timing->audio_milliseconds_per_frame)));
    return TRUE;
}

static void kw_apply_from_game_hook(KwRuntime *runtime, const char *source) {
    if (!kw_initialize_runtime_files(runtime)) return;
    kw_log_text("Game hook reached: ");
    kw_log_line(source);

    if (!kw_game_patches_install(&runtime->game, &runtime->config,
                                 kw_frame_pacer_stub())) {
        runtime->disabled = TRUE;
        kw_log_line("ERROR: disabling patch after static installation failure");
    } else if (!kw_apply_fixed_rate(runtime)) {
        runtime->disabled = TRUE;
        kw_log_line("ERROR: disabling patch after live-state initialization failure");
    }
}

static BOOL kw_install_bootstrap_hooks(KwRuntime *runtime) {
    const KwBootstrapLayout *bootstrap = &runtime->game.bootstrap;
    KwPatchTransaction transaction;
    kw_u8 runtime_patch[5] = {0xE8, 0, 0, 0, 0};
    kw_u8 session_patch[5] = {0xE9, 0, 0, 0, 0};

    kw_encode_rel32(&runtime_patch[1],
                    kw_game_address(&runtime->game,
                                    bootstrap->runtime_config_tail.instruction + 5u),
                    kw_runtime_config_tail_hook);
    kw_encode_rel32(&session_patch[1],
                    kw_game_address(&runtime->game,
                                    bootstrap->start_session_tail.instruction + 5u),
                    kw_start_session_tail_hook);
    kw_patch_transaction_init(&transaction);
    if (!kw_patch_transaction_write(
            &transaction,
            kw_game_address(&runtime->game,
                            bootstrap->runtime_config_tail.instruction),
            runtime_patch, sizeof(runtime_patch)) ||
        !kw_patch_transaction_write(
            &transaction,
            kw_game_address(&runtime->game,
                            bootstrap->start_session_tail.instruction),
            session_patch, sizeof(session_patch))) {
        kw_patch_transaction_rollback(&transaction);
        runtime->bootstrap_status = KW_BOOTSTRAP_WRITE_FAILED;
        return FALSE;
    }
    runtime->bootstrap_status = KW_BOOTSTRAP_INSTALLED;
    return TRUE;
}

BOOL kw_runtime_attach(HMODULE self_module, kw_u8 *game_module) {
    KwGameResolveResult result;

    g_runtime.self_module = self_module;
    result = kw_resolve_game_layout(&g_runtime.game, game_module);
    if (result == KW_GAME_INVALID_PE) {
        g_runtime.bootstrap_status = KW_BOOTSTRAP_BAD_PE;
        return FALSE;
    }
    if (result != KW_GAME_RESOLVED) {
        g_runtime.bootstrap_status = KW_BOOTSTRAP_BAD_PATCH_SITES;
        return FALSE;
    }
    return kw_install_bootstrap_hooks(&g_runtime);
}

void kw_runtime_proxy_checkpoint(void) {
    kw_initialize_runtime_files(&g_runtime);
    if (InterlockedCompareExchange(&g_runtime.proxy_checkpoint_logged, 1, 0) == 0) {
        kw_log_line("dinput8 proxy forwarding initialized");
    }
}

int KW_THISCALL kw_runtime_config_tail_hook(void *original_this) {
    KwRuntimeConfigTailFn original = (KwRuntimeConfigTailFn)kw_game_address(
        &g_runtime.game, g_runtime.game.bootstrap.runtime_config_tail.target);
    int result = original(original_this);
    kw_apply_from_game_hook(&g_runtime, "GameEngine_ApplyRuntimeConfiguration tail");
    return result;
}

int kw_start_session_tail_hook(void) {
    KwStartSessionTailFn original = (KwStartSessionTailFn)kw_game_address(
        &g_runtime.game, g_runtime.game.bootstrap.start_session_tail.target);
    int result = original();
    kw_apply_from_game_hook(&g_runtime, "GameEngine_StartGameSession tail");
    return result;
}
