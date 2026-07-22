#include "runtime.h"

#include "config.h"
#include "frame_pacer.h"
#include "game_layout.h"
#include "game_patches.h"
#include "log.h"
#include "memory_patch.h"

volatile LONG g_kw_bootstrap_status = KW_BOOTSTRAP_NOT_ATTEMPTED;

static volatile LONG g_runtime_init_state;
static volatile LONG g_proxy_checkpoint_logged;
static BOOL g_runtime_disabled;
static wchar_t g_ini_path[MAX_PATH];
static wchar_t g_log_path[MAX_PATH];

typedef int (KW_THISCALL *KwRuntimeConfigTailFn)(void *original_this);
typedef int (*KwStartSessionTailFn)(void);

static BOOL kw_initialize_runtime_files(void) {
    LONG state = InterlockedCompareExchange(&g_runtime_init_state, 1, 0);
    BOOL ini_found;
    if (state == 2) return TRUE;
    if (state == 1) {
        while (InterlockedCompareExchange(&g_runtime_init_state, 1, 1) == 1) Sleep(0);
        return g_runtime_init_state == 2;
    }

    /* This runs from normal game/proxy execution, never under the loader lock. */
    kw_config_set_defaults(&g_kw_config);
    if (GetModuleFileNameW(g_kw_self_module, g_ini_path, KW_ARRAY_COUNT(g_ini_path)) == 0 ||
        !kw_path_replace_filename(g_ini_path, KW_ARRAY_COUNT(g_ini_path), L"kw_fps_patch.ini") ||
        !kw_wide_copy(g_log_path, KW_ARRAY_COUNT(g_log_path), g_ini_path) ||
        !kw_path_replace_filename(g_log_path, KW_ARRAY_COUNT(g_log_path), L"kw_fps_patch.log")) {
        g_runtime_disabled = TRUE;
        InterlockedExchange(&g_runtime_init_state, 2);
        return FALSE;
    }

    ini_found = kw_config_load(&g_kw_config, g_ini_path);
    kw_log_open(g_log_path, g_kw_config.logging);
    kw_log_line("Kane's Wrath FPS patch bootstrap");
    kw_log_text("Resolved target: ");
    kw_log_line(g_kw_game_layout.build_name);
    kw_log_hex32("PE timestamp=", g_kw_game_layout.pe_timestamp);
    kw_log_hex32("PE image size=", g_kw_game_layout.pe_size_of_image);
    kw_log_line(ini_found ? "Configuration: kw_fps_patch.ini loaded" :
                            "Configuration: using built-in defaults");
    kw_log_u32("target_fps=", g_kw_config.target_fps);
    kw_log_u32("precise_pacing=", g_kw_config.precise_pacing != 0);
    kw_log_u32("bootstrap_status=", (kw_u32)g_kw_bootstrap_status);

    if (!g_kw_config.enabled) {
        kw_log_line("Patch disabled by configuration");
        g_runtime_disabled = TRUE;
    } else if (g_kw_bootstrap_status != KW_BOOTSTRAP_INSTALLED) {
        kw_log_line("ERROR: bootstrap callsite hooks were not installed");
        g_runtime_disabled = TRUE;
    }

    if (!g_runtime_disabled && !kw_frame_pacer_initialize(g_kw_config.precise_pacing)) {
        kw_log_line("ERROR: could not initialize the precise frame pacer");
        g_runtime_disabled = TRUE;
    }

    InterlockedExchange(&g_runtime_init_state, 2);
    return !g_runtime_disabled;
}

static BOOL kw_apply_fixed_rate(void) {
    const KwTimingLayout *timing = &g_kw_game_layout.timing;
    const KwPacingLayout *pacing = &g_kw_game_layout.pacing;
    kw_u8 *engine;
    kw_u8 *global_data;
    kw_u32 ratio = g_kw_config.target_fps / 15u;
    kw_u32 seed_ms = 1000u / g_kw_config.target_fps;
    float audio_milliseconds = 1000.0f / (float)g_kw_config.target_fps;
    float client_seconds = 1.0f / (float)g_kw_config.target_fps;
    kw_u32 i;
    engine = *(kw_u8 **)kw_game_address(timing->game_engine_pointer);
    global_data = *(kw_u8 **)kw_game_address(timing->global_data_pointer);
    if (engine == NULL || global_data == NULL) {
        kw_log_line("ERROR: live GameEngine or GlobalData pointer is null");
        return FALSE;
    }

    *(kw_u32 *)kw_game_address(timing->client_fps) = g_kw_config.target_fps;
    /*
     * The game caches three different client-rate conversions at CRT startup:
     *
     *   float(g_clientUpdateFPS)
     *   1000.0f / g_clientUpdateFPS  (audio milliseconds/client frame)
     *   1.0f / g_clientUpdateFPS     (visual seconds/client frame)
     *
     * All three must follow the raised client rate.  The audio value is
     * deliberately milliseconds, not seconds: SageAudioManager subtracts it
     * from XML-authored millisecond delays and passes integer millisecond steps
     * into its playback/fade machinery.  Supplying 1/targetFPS here makes the
     * value 1000 times too small and causes delayed multisounds to disappear or
     * arrive extremely late.  Supplying the retail 33.333 ms at 90 Hz instead
     * makes authored delays expire three times too early.
     *
     * Do not change the nearby 15 Hz logic constants or
     * g_gpuParticleFramesPerMillisecond (intentionally fixed at 0.03 for the
     * particle shaders' 30-frame time domain).
     */
    *(float *)kw_game_address(timing->client_fps_float) = (float)g_kw_config.target_fps;
    *(float *)kw_game_address(timing->audio_milliseconds_per_frame) = audio_milliseconds;
    *(float *)kw_game_address(timing->visual_seconds_per_frame) = client_seconds;
    *(kw_u32 *)kw_game_address(timing->w3d_milliseconds_per_frame) = seed_ms;
    global_data[KW_GLOBAL_DATA_USE_FPS_LIMIT] = 1;
    *(kw_u32 *)(global_data + KW_GLOBAL_DATA_FPS_LIMIT) = g_kw_config.target_fps;
    *(kw_u32 *)(engine + KW_ENGINE_MAX_UPDATE_FPS) = g_kw_config.target_fps;
    *(kw_u32 *)(engine + KW_ENGINE_NOMINAL_CLIENT_FRAMES_PER_LOGIC_TICK) = ratio;
    *(kw_u32 *)(engine + KW_ENGINE_PACING_UPDATE_MULTIPLIER) = ratio;
    for (i = 0; i < KW_ENGINE_FRAME_DURATION_HISTORY_COUNT; ++i) {
        *(kw_u32 *)(engine + KW_ENGINE_FRAME_DURATION_HISTORY_MS + i * 4u) = seed_ms;
    }
    *(kw_u32 *)(engine + KW_ENGINE_FRAME_DURATION_HISTORY_SUM_MS) =
        seed_ms * KW_ENGINE_FRAME_DURATION_HISTORY_COUNT;
    *(kw_u32 *)(engine + KW_ENGINE_FRAME_DURATION_HISTORY_INDEX) = 0;
    *(kw_u32 *)kw_game_address(pacing->previous_frame_time_ms) = timeGetTime();
    kw_game_patches_reset_state();
    kw_frame_pacer_reset();
    kw_log_u32("Applied client FPS=", g_kw_config.target_fps);
    kw_log_u32("Applied W3D milliseconds/client-frame=", seed_ms);
    kw_log_hex32("Applied audio milliseconds/client-frame bits=",
                 kw_load_u32(kw_game_address(timing->audio_milliseconds_per_frame)));
    return TRUE;
}

static void kw_apply_from_game_hook(const char *source) {
    if (!kw_initialize_runtime_files() || g_runtime_disabled) return;
    kw_log_text("Game hook reached: ");
    kw_log_line(source);
    if (!kw_game_patches_install(kw_frame_pacer_stub())) {
        g_runtime_disabled = TRUE;
        kw_log_line("ERROR: disabling patch after static installation failure");
        return;
    }
    if (!kw_apply_fixed_rate()) {
        g_runtime_disabled = TRUE;
        kw_log_line("ERROR: disabling patch after live-state initialization failure");
    }
}

BOOL kw_install_bootstrap_hooks(void) {
    const KwBootstrapLayout *bootstrap;
    KwPatchTransaction transaction;
    kw_u8 runtime_patch[5] = {0xE8, 0, 0, 0, 0};
    kw_u8 session_patch[5] = {0xE9, 0, 0, 0, 0};
    /* DllMain resolves guarded signatures and installs two five-byte redirections. */
    if (!kw_validate_game_pe_headers(g_kw_game_module)) {
        InterlockedExchange(&g_kw_bootstrap_status, KW_BOOTSTRAP_BAD_PE);
        return FALSE;
    }
    if (!kw_resolve_game_layout(g_kw_game_module)) {
        InterlockedExchange(&g_kw_bootstrap_status, KW_BOOTSTRAP_BAD_PATCH_SITES);
        return FALSE;
    }
    bootstrap = &g_kw_game_layout.bootstrap;
    kw_encode_rel32(&runtime_patch[1],
                    kw_game_address(bootstrap->runtime_config_tail.instruction + 5u),
                    kw_runtime_config_tail_hook);
    kw_encode_rel32(&session_patch[1],
                    kw_game_address(bootstrap->start_session_tail.instruction + 5u),
                    kw_start_session_tail_hook);
    kw_patch_transaction_init(&transaction);
    if (!kw_patch_transaction_write(
            &transaction, kw_game_address(bootstrap->runtime_config_tail.instruction),
            runtime_patch, sizeof(runtime_patch)) ||
        !kw_patch_transaction_write(
            &transaction, kw_game_address(bootstrap->start_session_tail.instruction),
            session_patch, sizeof(session_patch))) {
        kw_patch_transaction_rollback(&transaction);
        InterlockedExchange(&g_kw_bootstrap_status, KW_BOOTSTRAP_WRITE_FAILED);
        return FALSE;
    }
    InterlockedExchange(&g_kw_bootstrap_status, KW_BOOTSTRAP_INSTALLED);
    return TRUE;
}

void kw_runtime_proxy_checkpoint(void) {
    kw_initialize_runtime_files();
    if (InterlockedCompareExchange(&g_proxy_checkpoint_logged, 1, 0) == 0) {
        kw_log_line("dinput8 proxy forwarding initialized");
    }
}

int KW_THISCALL kw_runtime_config_tail_hook(void *original_this) {
    KwRuntimeConfigTailFn original =
        (KwRuntimeConfigTailFn)kw_game_address(
            g_kw_game_layout.bootstrap.runtime_config_tail.target);
    int result = original(original_this);
    kw_apply_from_game_hook("GameEngine_ApplyRuntimeConfiguration tail");
    return result;
}

int kw_start_session_tail_hook(void) {
    KwStartSessionTailFn original =
        (KwStartSessionTailFn)kw_game_address(
            g_kw_game_layout.bootstrap.start_session_tail.target);
    int result = original();
    kw_apply_from_game_hook("GameEngine_StartGameSession tail");
    return result;
}
