#include "runtime.h"

#include "config.h"
#include "game_layout.h"
#include "log.h"
#include "memory_patch.h"

volatile LONG g_kw_bootstrap_status = KW_BOOTSTRAP_NOT_ATTEMPTED;

static volatile LONG g_runtime_init_state;
static volatile LONG g_proxy_checkpoint_logged;
static BOOL g_static_patches_installed;
static BOOL g_runtime_disabled;
static float g_visual_client_step;
static float g_client_frames_per_millisecond;
static kw_u32 g_visual_reference_fps = 30u;
static kw_u32 g_fx_particle_update_accumulator;
static kw_u8 *g_pacing_stub;
static LARGE_INTEGER g_qpc_frequency;
static LARGE_INTEGER g_next_deadline;
static BOOL g_qpc_available;
static BOOL g_qpc_armed;
static wchar_t g_ini_path[MAX_PATH];
static wchar_t g_log_path[MAX_PATH];

typedef int (KW_THISCALL *KwRuntimeConfigTailFn)(void *original_this);
typedef int (*KwStartSessionTailFn)(void);
typedef kw_u32 (KW_THISCALL *KwGameClientGetFrameNumberFn)(void *game_client);
typedef void (KW_THISCALL *KwSubsystemUpdateFn)(void *subsystem);

/*
 * Translate the high-rate GameClient frame counter into the 30 Hz frame
 * domain used by legacy frame-authored visual systems.
 *
 * This is a drop-in replacement for the five-byte virtual call sequence
 * `mov eax,[ecx]; call [eax+0x78]`: ECX is still the GameClient pointer and
 * EAX still receives an unsigned frame number.  The ceil mapping makes frame
 * 1 immediately visible, then repeats synthetic frames as needed:
 *
 *   target 90:  actual 1,2,3,4,... -> retail 1,1,1,2,...
 *   target 45:  actual 1,2,3,4,... -> retail 1,2,2,3,...
 *
 * Consumers already contain duplicate-frame guards, so they keep rendering
 * every client frame while advancing authored state only 30 times/second.
 */
static kw_u32 KW_THISCALL kw_get_retail_visual_frame(void *game_client) {
    void **vtable;
    KwGameClientGetFrameNumberFn get_frame_number;
    kw_u32 actual_frame;
    kw_u32 whole_intervals;
    kw_u32 remainder;
    kw_u32 target_fps = g_kw_config.target_fps;
    if (game_client == NULL || target_fps < g_visual_reference_fps) return 0;
    vtable = *(void ***)game_client;
    get_frame_number = (KwGameClientGetFrameNumberFn)vtable[0x78u / sizeof(void *)];
    actual_frame = get_frame_number(game_client);
    /* Keep this CRT-free: split the exact ceil(frame * 30 / target) into
       32-bit quotient/remainder operations instead of pulling in __udivdi3. */
    whole_intervals = actual_frame / target_fps;
    remainder = actual_frame % target_fps;
    return whole_intervals * g_visual_reference_fps +
           (remainder * g_visual_reference_fps + target_fps - 1u) / target_fps;
}

static void KW_THISCALL kw_fx_particle_simulation_update_at_retail_rate(void *manager) {
    KwSubsystemUpdateFn update;
    kw_u32 target_fps = g_kw_config.target_fps;
    if (manager == NULL) return;
    update = (KwSubsystemUpdateFn)(g_kw_game_module + KW_RVA_FX_PARTICLE_SIMULATION_TARGET);
    if (target_fps <= g_visual_reference_fps) {
        update(manager);
        return;
    }
    g_fx_particle_update_accumulator += g_visual_reference_fps;
    if (g_fx_particle_update_accumulator >= target_fps) {
        g_fx_particle_update_accumulator -= target_fps;
        update(manager);
    }
}

static kw_u32 kw_read_u32(const void *address) {
    kw_u32 value;
    memcpy(&value, address, sizeof(value));
    return value;
}

static BOOL kw_validate_runtime_bytes(const char *site_name, kw_u32 rva,
                                      const kw_u8 *expected, size_t size) {
    const kw_u8 *actual = g_kw_game_module + rva;
    if (memcmp(actual, expected, size) == 0) return TRUE;
    kw_log_text("ERROR: runtime code mismatch at ");
    kw_log_line(site_name);
    kw_log_hex32("  RVA=", rva);
    if (size >= 4u) {
        kw_log_hex32("  actual first four bytes=", kw_read_u32(actual));
        kw_log_hex32("  expected first four bytes=", kw_read_u32(expected));
    }
    return FALSE;
}

static BOOL kw_validate_runtime_u32(const char *site_name, kw_u32 rva, kw_u32 expected) {
    kw_u32 actual = kw_read_u32(g_kw_game_module + rva);
    if (actual == expected) return TRUE;
    kw_log_text("ERROR: runtime value mismatch at ");
    kw_log_line(site_name);
    kw_log_hex32("  RVA=", rva);
    kw_log_hex32("  actual=", actual);
    kw_log_hex32("  expected=", expected);
    return FALSE;
}

static BOOL kw_validate_relative_target(const char *site_name, kw_u32 rva,
                                        kw_u8 opcode, kw_u32 expected_target_rva) {
    kw_i32 displacement;
    kw_u32 actual_target_rva;
    if (g_kw_game_module[rva] != opcode) {
        kw_log_text("ERROR: runtime branch opcode mismatch at ");
        kw_log_line(site_name);
        kw_log_hex32("  RVA=", rva);
        return FALSE;
    }
    memcpy(&displacement, g_kw_game_module + rva + 1u, sizeof(displacement));
    actual_target_rva = rva + 5u + (kw_u32)displacement;
    if (actual_target_rva == expected_target_rva) return TRUE;
    kw_log_text("ERROR: runtime branch target mismatch at ");
    kw_log_line(site_name);
    kw_log_hex32("  actual target RVA=", actual_target_rva);
    kw_log_hex32("  expected target RVA=", expected_target_rva);
    return FALSE;
}

static void kw_store_u32(kw_u8 *bytes, kw_u32 value) {
    bytes[0] = (kw_u8)value;
    bytes[1] = (kw_u8)(value >> 8);
    bytes[2] = (kw_u8)(value >> 16);
    bytes[3] = (kw_u8)(value >> 24);
}

static void kw_store_rel32(kw_u8 *operand, const kw_u8 *next_instruction, const void *target) {
    intptr_t displacement = (const kw_u8 *)target - next_instruction;
    kw_store_u32(operand, (kw_u32)(kw_i32)displacement);
}

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

    g_qpc_available = QueryPerformanceFrequency(&g_qpc_frequency) &&
                      g_qpc_frequency.HighPart == 0 && g_qpc_frequency.LowPart != 0;
    if (!g_qpc_available && g_kw_config.precise_pacing) {
        kw_log_line("ERROR: QPC frequency is unavailable or outside the supported range");
        g_runtime_disabled = TRUE;
    }

    InterlockedExchange(&g_runtime_init_state, 2);
    return !g_runtime_disabled;
}

static BOOL kw_validate_runtime_patch_sites(void) {
    static const kw_u8 display_branch[2] = {0x7D, 0x13};
    static const kw_u8 tracer_reset_get_frame[5] = {0x8B, 0x01, 0xFF, 0x50, 0x78};
    static const kw_u8 tracer_update_get_frame[6] = {0x8B, 0x01, 0x57, 0xFF, 0x50, 0x78};
    static const kw_u8 virtual_get_frame[5] = {0x8B, 0x01, 0xFF, 0x50, 0x78};
    static const kw_u8 gpu_particle_frame_rate_opcode[3] = {0x0F, 0xAF, 0x05};
    static const kw_u8 radius_cursor_throb_frame_rate_opcode[2] = {0xD8, 0x0D};
    static const kw_u8 pacing_prefix[2] = {0x80, 0x3D};
    static const kw_u8 pacing_suffix[3] = {0x00, 0x74, 0x69};
    kw_u32 retail_visual = (kw_u32)(uintptr_t)(g_kw_game_module + KW_RVA_RETAIL_VISUAL_STEP);
    kw_u32 pacing_flag =
        (kw_u32)(uintptr_t)(g_kw_game_module + KW_RVA_ENFORCE_FPS_LIMIT_THIS_FRAME);
    BOOL valid = TRUE;

    /*
     * Compatibility note: community launchers may rewrite client timing data
     * before this post-configuration hook executes.  Those values are not
     * patch identities: kw_apply_fixed_rate() intentionally replaces them a
     * few instructions later.  Log their incoming state for diagnostics, but
     * do not reject an otherwise identical executable merely because another
     * patch already selected a client FPS or recomputed its client-only
     * caches.  The authoritative 15 Hz logic rate remains a hard invariant.
     */
    kw_log_u32("Incoming client FPS=", kw_read_u32(g_kw_game_module + KW_RVA_CLIENT_UPDATE_FPS));
    kw_log_u32("Incoming W3D milliseconds/client-frame=",
               kw_read_u32(g_kw_game_module + KW_RVA_W3D_MILLISECONDS_PER_CLIENT_FRAME));
    kw_log_hex32("Incoming client-FPS float bits=",
                 kw_read_u32(g_kw_game_module + KW_RVA_CLIENT_FPS_FLOAT));
    kw_log_hex32("Incoming audio milliseconds/client-frame bits=",
                 kw_read_u32(g_kw_game_module + KW_RVA_AUDIO_MILLISECONDS_PER_CLIENT_FRAME));
    kw_log_hex32("Incoming visual seconds/client-frame bits=",
                 kw_read_u32(g_kw_game_module + KW_RVA_VISUAL_SECONDS_PER_CLIENT_FRAME));

    if (!kw_validate_runtime_u32("g_logicFPS", KW_RVA_LOGIC_FPS, 15u)) valid = FALSE;
    if (!kw_validate_runtime_bytes("display limiter branch", KW_RVA_DISPLAY_LIMITER_BRANCH,
                                   display_branch, sizeof(display_branch))) valid = FALSE;
    if (!kw_validate_runtime_u32("camera visual-step operand",
                                 KW_RVA_VISUAL_STEP_CAMERA_OPERAND, retail_visual)) valid = FALSE;
    if (!kw_validate_runtime_u32("laser visual-step operand",
                                 KW_RVA_VISUAL_STEP_LASER_OPERAND, retail_visual)) valid = FALSE;
    if (!kw_validate_runtime_u32("scripted-model visual-step operand",
                                 KW_RVA_VISUAL_STEP_MODEL_OPERAND, retail_visual)) valid = FALSE;
    if (!kw_validate_runtime_bytes("tracer reset frame read", KW_RVA_TRACER_RESET_GET_FRAME,
                                   tracer_reset_get_frame, sizeof(tracer_reset_get_frame))) valid = FALSE;
    if (!kw_validate_runtime_bytes("tracer update frame read", KW_RVA_TRACER_UPDATE_GET_FRAME,
                                   tracer_update_get_frame, sizeof(tracer_update_get_frame))) valid = FALSE;
    if (!kw_validate_runtime_bytes("cloud-effect frame read", KW_RVA_CLOUD_EFFECT_GET_FRAME,
                                   virtual_get_frame, sizeof(virtual_get_frame))) valid = FALSE;
    if (!kw_validate_runtime_bytes("Anim2D timestamp frame read",
                                   KW_RVA_ANIM2D_SET_FRAME_GET_FRAME,
                                   virtual_get_frame, sizeof(virtual_get_frame))) valid = FALSE;
    if (!kw_validate_runtime_bytes("Anim2D update frame read", KW_RVA_ANIM2D_UPDATE_GET_FRAME,
                                   virtual_get_frame, sizeof(virtual_get_frame))) valid = FALSE;
    if (!kw_validate_relative_target("FX particle simulation call",
                                     KW_RVA_FX_PARTICLE_SIMULATION_CALL, 0xE8,
                                     KW_RVA_FX_PARTICLE_SIMULATION_TARGET)) valid = FALSE;
    if (!kw_validate_runtime_bytes("GPU particle expiry instruction",
                                   KW_RVA_GPU_PARTICLE_FRAME_RATE_INSTRUCTION,
                                   gpu_particle_frame_rate_opcode,
                                   sizeof(gpu_particle_frame_rate_opcode))) valid = FALSE;
    if (!kw_validate_runtime_u32("GPU particle client-FPS operand",
                                 KW_RVA_GPU_PARTICLE_FRAME_RATE_OPERAND,
                                 (kw_u32)(uintptr_t)(g_kw_game_module +
                                                    KW_RVA_CLIENT_UPDATE_FPS))) valid = FALSE;
    if (!kw_validate_runtime_bytes("radius-cursor throb frame-rate instruction",
                                   KW_RVA_RADIUS_CURSOR_THROB_FRAME_RATE_INSTRUCTION,
                                   radius_cursor_throb_frame_rate_opcode,
                                   sizeof(radius_cursor_throb_frame_rate_opcode))) valid = FALSE;
    if (!kw_validate_runtime_u32(
            "radius-cursor throb frames-per-millisecond operand",
            KW_RVA_RADIUS_CURSOR_THROB_FRAME_RATE_OPERAND,
            (kw_u32)(uintptr_t)(g_kw_game_module +
                               KW_RVA_LEGACY_VISUAL_FRAMES_PER_MILLISECOND))) valid = FALSE;
    if (g_kw_config.precise_pacing) {
        if (!kw_validate_runtime_bytes("outer pacing gate opcode", KW_RVA_OUTER_PACING_GATE,
                                       pacing_prefix, sizeof(pacing_prefix))) valid = FALSE;
        if (!kw_validate_runtime_u32("outer pacing gate flag operand",
                                     KW_RVA_OUTER_PACING_GATE + 2u, pacing_flag)) valid = FALSE;
        if (!kw_validate_runtime_bytes("outer pacing gate branch",
                                       KW_RVA_OUTER_PACING_GATE + 6u,
                                       pacing_suffix, sizeof(pacing_suffix))) valid = FALSE;
    }
    return valid;
}

static BOOL kw_build_pacing_stub(void);

static BOOL kw_install_static_patches(void) {
    static const kw_u8 display_original[2] = {0x7D, 0x13};
    static const kw_u8 display_patch[2] = {0xEB, 0x13};
    static const kw_u8 tracer_reset_original[5] = {0x8B, 0x01, 0xFF, 0x50, 0x78};
    static const kw_u8 tracer_update_original[6] = {0x8B, 0x01, 0x57, 0xFF, 0x50, 0x78};
    static const kw_u8 virtual_get_frame_original[5] = {0x8B, 0x01, 0xFF, 0x50, 0x78};
    kw_u8 fx_particle_original[5];
    kw_u8 tracer_reset_patch[5] = {0xE8, 0, 0, 0, 0};
    kw_u8 tracer_update_patch[6] = {0x57, 0xE8, 0, 0, 0, 0};
    kw_u8 cloud_effect_patch[5] = {0xE8, 0, 0, 0, 0};
    kw_u8 anim2d_set_frame_patch[5] = {0xE8, 0, 0, 0, 0};
    kw_u8 anim2d_update_patch[5] = {0xE8, 0, 0, 0, 0};
    kw_u8 fx_particle_patch[5] = {0xE8, 0, 0, 0, 0};
    kw_u8 pacing_original[9] = {0x80, 0x3D, 0, 0, 0, 0, 0x00, 0x74, 0x69};
    kw_u8 pacing_patch[9] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90};
    kw_u32 retail_visual = (kw_u32)(uintptr_t)(g_kw_game_module + KW_RVA_RETAIL_VISUAL_STEP);
    kw_u32 replacement_visual = (kw_u32)(uintptr_t)&g_visual_client_step;
    kw_u32 retail_client_fps = (kw_u32)(uintptr_t)(g_kw_game_module + KW_RVA_CLIENT_UPDATE_FPS);
    kw_u32 retail_legacy_frames_per_millisecond =
        (kw_u32)(uintptr_t)(g_kw_game_module +
                            KW_RVA_LEGACY_VISUAL_FRAMES_PER_MILLISECOND);
    kw_u32 replacement_reference_fps = (kw_u32)(uintptr_t)&g_visual_reference_fps;
    kw_u32 replacement_client_frames_per_millisecond =
        (kw_u32)(uintptr_t)&g_client_frames_per_millisecond;
    kw_store_u32(&pacing_original[2],
                 (kw_u32)(uintptr_t)(g_kw_game_module + KW_RVA_ENFORCE_FPS_LIMIT_THIS_FRAME));
    BOOL camera_written = FALSE, laser_written = FALSE, model_written = FALSE;
    BOOL tracer_reset_written = FALSE, tracer_update_written = FALSE;
    BOOL cloud_effect_written = FALSE;
    BOOL anim2d_set_frame_written = FALSE, anim2d_update_written = FALSE;
    BOOL fx_particle_written = FALSE, gpu_particle_written = FALSE;
    BOOL radius_cursor_throb_written = FALSE;
    BOOL display_written = FALSE, pacing_written = FALSE;

    if (g_static_patches_installed) return TRUE;

    memcpy(fx_particle_original,
           g_kw_game_module + KW_RVA_FX_PARTICLE_SIMULATION_CALL,
           sizeof(fx_particle_original));

    /* Validate the complete transaction before changing the first game byte. */
    if (!kw_validate_runtime_patch_sites()) {
        kw_log_line("ERROR: one or more runtime patch sites did not match expected bytes/state");
        return FALSE;
    }
    if (g_kw_config.precise_pacing && !kw_build_pacing_stub()) {
        kw_log_line("ERROR: could not build the QPC pacing stub");
        return FALSE;
    }

    g_visual_client_step = 1.0f / (float)g_kw_config.target_fps;
    g_client_frames_per_millisecond = (float)g_kw_config.target_fps / 1000.0f;
    if (!kw_write_protected(g_kw_game_module + KW_RVA_VISUAL_STEP_CAMERA_OPERAND,
                            &replacement_visual, sizeof(replacement_visual))) goto rollback;
    camera_written = TRUE;
    if (!kw_write_protected(g_kw_game_module + KW_RVA_VISUAL_STEP_LASER_OPERAND,
                            &replacement_visual, sizeof(replacement_visual))) goto rollback;
    laser_written = TRUE;
    if (!kw_write_protected(g_kw_game_module + KW_RVA_VISUAL_STEP_MODEL_OPERAND,
                            &replacement_visual, sizeof(replacement_visual))) goto rollback;
    model_written = TRUE;

    /*
     * Tracer reset: replace `mov eax,[ecx]; call [eax+78h]` with a direct
     * call.  ECX was loaded from g_gameClient immediately before this site.
     */
    kw_store_rel32(&tracer_reset_patch[1],
                   g_kw_game_module + KW_RVA_TRACER_RESET_GET_FRAME + 5,
                   kw_get_retail_visual_frame);
    if (!kw_write_protected(g_kw_game_module + KW_RVA_TRACER_RESET_GET_FRAME,
                            tracer_reset_patch, sizeof(tracer_reset_patch))) goto rollback;
    tracer_reset_written = TRUE;
    /* The leading PUSH EDI reproduces the byte displaced at this site. */
    kw_store_rel32(&tracer_update_patch[2],
                   g_kw_game_module + KW_RVA_TRACER_UPDATE_GET_FRAME + 6,
                   kw_get_retail_visual_frame);
    if (!kw_write_protected(g_kw_game_module + KW_RVA_TRACER_UPDATE_GET_FRAME,
                            tracer_update_patch, sizeof(tracer_update_patch))) goto rollback;
    tracer_update_written = TRUE;

    /*
     * Cloud/lightning duration, chance and frequency are evaluated once per
     * distinct frame number.  Feeding a synthetic 30 Hz frame lets the stock
     * duplicate-frame guard skip the extra 45/90 Hz client updates.
     */
    kw_store_rel32(&cloud_effect_patch[1],
                   g_kw_game_module + KW_RVA_CLOUD_EFFECT_GET_FRAME + 5,
                   kw_get_retail_visual_frame);
    if (!kw_write_protected(g_kw_game_module + KW_RVA_CLOUD_EFFECT_GET_FRAME,
                            cloud_effect_patch, sizeof(cloud_effect_patch))) goto rollback;
    cloud_effect_written = TRUE;

    /*
     * Anim2D stores a frame-number timestamp when its sprite frame changes,
     * then subtracts that timestamp during collection updates.  Both reads
     * must use the same clock; patching only the update side would mix a raw
     * 45/90 Hz start frame with a synthetic 30 Hz current frame and underflow.
     */
    kw_store_rel32(&anim2d_set_frame_patch[1],
                   g_kw_game_module + KW_RVA_ANIM2D_SET_FRAME_GET_FRAME + 5,
                   kw_get_retail_visual_frame);
    if (!kw_write_protected(g_kw_game_module + KW_RVA_ANIM2D_SET_FRAME_GET_FRAME,
                            anim2d_set_frame_patch, sizeof(anim2d_set_frame_patch))) goto rollback;
    anim2d_set_frame_written = TRUE;
    kw_store_rel32(&anim2d_update_patch[1],
                   g_kw_game_module + KW_RVA_ANIM2D_UPDATE_GET_FRAME + 5,
                   kw_get_retail_visual_frame);
    if (!kw_write_protected(g_kw_game_module + KW_RVA_ANIM2D_UPDATE_GET_FRAME,
                            anim2d_update_patch, sizeof(anim2d_update_patch))) goto rollback;
    anim2d_update_written = TRUE;

    /*
     * This call is deliberately inside W3DParticleSystemManager's update.
     * Only legacy particle simulation is throttled to 30 Hz; render-buffer
     * preparation surrounding it continues on every high-rate client frame.
     */
    kw_store_rel32(&fx_particle_patch[1],
                   g_kw_game_module + KW_RVA_FX_PARTICLE_SIMULATION_CALL + 5,
                   kw_fx_particle_simulation_update_at_retail_rate);
    if (!kw_write_protected(g_kw_game_module + KW_RVA_FX_PARTICLE_SIMULATION_CALL,
                            fx_particle_patch, sizeof(fx_particle_patch))) goto rollback;
    fx_particle_written = TRUE;
    /*
     * Resolved instruction:
     *   imul eax, dword ptr [g_clientUpdateFPS]
     * Redirect only its absolute operand to the DLL-owned constant 30.  GPU
     * creation and shaders already use 30 frame units/second (0.03 per ms).
     */
    if (!kw_write_protected(g_kw_game_module + KW_RVA_GPU_PARTICLE_FRAME_RATE_OPERAND,
                            &replacement_reference_fps, sizeof(replacement_reference_fps))) goto rollback;
    gpu_particle_written = TRUE;

    /*
     * RadiusDecalInstance_UpdateForClientFrame converts the XML-authored
     * OpacityThrobTime from milliseconds to a frame-counted period.  The
     * retail operand points at 0.03 frames/ms, which is correct only while
     * the raw client-frame counter advances at 30 Hz.  Redirect this one
     * consumer to targetFPS/1000; do not change the shared retail scalar,
     * because GPU particle creation intentionally remains in a 30 Hz domain.
     */
    if (!kw_write_protected(
            g_kw_game_module + KW_RVA_RADIUS_CURSOR_THROB_FRAME_RATE_OPERAND,
            &replacement_client_frames_per_millisecond,
            sizeof(replacement_client_frames_per_millisecond))) goto rollback;
    radius_cursor_throb_written = TRUE;

    if (!kw_write_protected(g_kw_game_module + KW_RVA_DISPLAY_LIMITER_BRANCH,
                            display_patch, sizeof(display_patch))) goto rollback;
    display_written = TRUE;

    if (g_kw_config.precise_pacing) {
        kw_store_rel32(&pacing_patch[1], g_kw_game_module + KW_RVA_OUTER_PACING_GATE + 5,
                       g_pacing_stub);
        if (!kw_write_protected(g_kw_game_module + KW_RVA_OUTER_PACING_GATE,
                                pacing_patch, sizeof(pacing_patch))) goto rollback;
        pacing_written = TRUE;
    }

    g_static_patches_installed = TRUE;
    kw_log_line("Static visual and limiter patches installed");
    kw_log_line("Frame-counted particles, tracers, clouds and Anim2D pinned to retail 30 Hz");
    kw_log_line("Radius-cursor opacity throb converted using the live client FPS");
    return TRUE;

rollback:
    kw_log_line("ERROR: patch write failed; attempting rollback");
    if (pacing_written) kw_write_protected(g_kw_game_module + KW_RVA_OUTER_PACING_GATE,
                                           pacing_original, sizeof(pacing_original));
    if (display_written) kw_write_protected(g_kw_game_module + KW_RVA_DISPLAY_LIMITER_BRANCH,
                                            display_original, sizeof(display_original));
    if (radius_cursor_throb_written) kw_write_protected(
        g_kw_game_module + KW_RVA_RADIUS_CURSOR_THROB_FRAME_RATE_OPERAND,
        &retail_legacy_frames_per_millisecond,
        sizeof(retail_legacy_frames_per_millisecond));
    if (gpu_particle_written) kw_write_protected(
        g_kw_game_module + KW_RVA_GPU_PARTICLE_FRAME_RATE_OPERAND,
        &retail_client_fps, sizeof(retail_client_fps));
    if (fx_particle_written) kw_write_protected(
        g_kw_game_module + KW_RVA_FX_PARTICLE_SIMULATION_CALL,
        fx_particle_original, sizeof(fx_particle_original));
    if (anim2d_update_written) kw_write_protected(
        g_kw_game_module + KW_RVA_ANIM2D_UPDATE_GET_FRAME,
        virtual_get_frame_original, sizeof(virtual_get_frame_original));
    if (anim2d_set_frame_written) kw_write_protected(
        g_kw_game_module + KW_RVA_ANIM2D_SET_FRAME_GET_FRAME,
        virtual_get_frame_original, sizeof(virtual_get_frame_original));
    if (cloud_effect_written) kw_write_protected(
        g_kw_game_module + KW_RVA_CLOUD_EFFECT_GET_FRAME,
        virtual_get_frame_original, sizeof(virtual_get_frame_original));
    if (tracer_update_written) kw_write_protected(
        g_kw_game_module + KW_RVA_TRACER_UPDATE_GET_FRAME,
        tracer_update_original, sizeof(tracer_update_original));
    if (tracer_reset_written) kw_write_protected(
        g_kw_game_module + KW_RVA_TRACER_RESET_GET_FRAME,
        tracer_reset_original, sizeof(tracer_reset_original));
    if (model_written) kw_write_protected(g_kw_game_module + KW_RVA_VISUAL_STEP_MODEL_OPERAND,
                                          &retail_visual, sizeof(retail_visual));
    if (laser_written) kw_write_protected(g_kw_game_module + KW_RVA_VISUAL_STEP_LASER_OPERAND,
                                          &retail_visual, sizeof(retail_visual));
    if (camera_written) kw_write_protected(g_kw_game_module + KW_RVA_VISUAL_STEP_CAMERA_OPERAND,
                                           &retail_visual, sizeof(retail_visual));
    return FALSE;
}

static void kw_reset_qpc_deadline(void) {
    g_qpc_armed = FALSE;
    g_next_deadline.QuadPart = 0;
}

static BOOL kw_apply_fixed_rate(void) {
    kw_u8 *engine;
    kw_u8 *global_data;
    kw_u32 ratio = g_kw_config.target_fps / 15u;
    kw_u32 seed_ms = 1000u / g_kw_config.target_fps;
    float audio_milliseconds = 1000.0f / (float)g_kw_config.target_fps;
    float client_seconds = 1.0f / (float)g_kw_config.target_fps;
    kw_u32 i;
    engine = *(kw_u8 **)(g_kw_game_module + KW_RVA_GAME_ENGINE_POINTER);
    global_data = *(kw_u8 **)(g_kw_game_module + KW_RVA_GLOBAL_DATA_POINTER);
    if (engine == NULL || global_data == NULL) {
        kw_log_line("ERROR: live GameEngine or GlobalData pointer is null");
        return FALSE;
    }

    *(kw_u32 *)(g_kw_game_module + KW_RVA_CLIENT_UPDATE_FPS) = g_kw_config.target_fps;
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
    *(float *)(g_kw_game_module + KW_RVA_CLIENT_FPS_FLOAT) =
        (float)g_kw_config.target_fps;
    *(float *)(g_kw_game_module + KW_RVA_AUDIO_MILLISECONDS_PER_CLIENT_FRAME) =
        audio_milliseconds;
    *(float *)(g_kw_game_module + KW_RVA_VISUAL_SECONDS_PER_CLIENT_FRAME) = client_seconds;
    *(kw_u32 *)(g_kw_game_module + KW_RVA_W3D_MILLISECONDS_PER_CLIENT_FRAME) = seed_ms;
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
    *(kw_u32 *)(g_kw_game_module + KW_RVA_PREVIOUS_ENGINE_FRAME_TIME_MS) = timeGetTime();
    g_fx_particle_update_accumulator = g_kw_config.target_fps - g_visual_reference_fps;
    kw_reset_qpc_deadline();
    kw_log_u32("Applied client FPS=", g_kw_config.target_fps);
    kw_log_u32("Applied W3D milliseconds/client-frame=", seed_ms);
    kw_log_hex32("Applied audio milliseconds/client-frame bits=",
                 kw_read_u32(g_kw_game_module +
                             KW_RVA_AUDIO_MILLISECONDS_PER_CLIENT_FRAME));
    return TRUE;
}

void KW_STDCALL kw_pace_client_frame(void *engine_pointer) {
    kw_u8 *engine = (kw_u8 *)engine_pointer;
    DWORD before_ms = timeGetTime();
    DWORD previous_ms = *(volatile DWORD *)(g_kw_game_module + KW_RVA_PREVIOUS_ENGINE_FRAME_TIME_MS);
    DWORD work_ms = before_ms - previous_ms;
    kw_i32 multiplier = *(volatile kw_i32 *)(engine + KW_ENGINE_PACING_UPDATE_MULTIPLIER);
    float scale = *(volatile float *)(g_kw_game_module + KW_RVA_NETWORK_FRAME_PACING_SCALE);
    float logic_ms = *(volatile float *)(g_kw_game_module + KW_RVA_MILLISECONDS_PER_LOGIC_FRAME);
    float period_ticks_f;
    kw_u32 period_ticks;
    kw_u32 spin_ticks;
    kw_u32 one_ms_ticks;
    LARGE_INTEGER now;
    DWORD after_ms;
    DWORD wait_ms;

    if (multiplier < 1) multiplier = 1;
    if (!(scale >= 0.5f)) scale = 0.5f;
    if (scale > 1.0f) scale = 1.0f;
    period_ticks_f = ((float)g_qpc_frequency.LowPart * logic_ms) /
                     (1000.0f * (float)multiplier * scale);
    period_ticks = (kw_u32)(period_ticks_f + 0.5f);
    if (period_ticks == 0) period_ticks = 1;
    one_ms_ticks = g_qpc_frequency.LowPart / 1000u;
    spin_ticks = (one_ms_ticks * g_kw_config.spin_threshold_us) / 1000u;

    QueryPerformanceCounter(&now);
    if (!g_qpc_armed) {
        g_next_deadline = now;
        g_qpc_armed = TRUE;
    }
    g_next_deadline.QuadPart += period_ticks;
    /* Do not try to replay expired deadlines after pause/load/breakpoint gaps. */
    if (now.QuadPart > g_next_deadline.QuadPart + (LONGLONG)period_ticks * 4) {
        g_next_deadline = now;
    }

    for (;;) {
        LONGLONG remaining;
        QueryPerformanceCounter(&now);
        remaining = g_next_deadline.QuadPart - now.QuadPart;
        if (remaining <= 0) break;
        if ((kw_u64)remaining > (kw_u64)spin_ticks + (kw_u64)one_ms_ticks * 2u) {
            Sleep(1);
        } else if ((kw_u64)remaining > spin_ticks) {
            if (!SwitchToThread()) Sleep(0);
        }
    }

    after_ms = timeGetTime();
    wait_ms = after_ms - before_ms;
    *(volatile DWORD *)(g_kw_game_module + KW_RVA_LAST_ENGINE_FRAME_DURATION_MS) = work_ms;
    *(volatile DWORD *)(g_kw_game_module + KW_RVA_LAST_LIMITER_WAIT_MS) = wait_ms;
    *(volatile DWORD *)(g_kw_game_module + KW_RVA_TOTAL_LIMITER_WAIT_MS) += wait_ms;
    *(volatile DWORD *)(g_kw_game_module + KW_RVA_PREVIOUS_ENGINE_FRAME_TIME_MS) = after_ms;
}

static BOOL kw_build_pacing_stub(void) {
    kw_u8 *stub;
    if (g_pacing_stub != NULL) return TRUE;
    if (!kw_allocate_executable_stub(32, &stub)) return FALSE;
    /*
     * cmp byte ptr [g_enforceFPSLimitThisFrame], 0
     * je no_limit; push esi; call pacer; jmp history; no_limit: jmp stock path
     */
    stub[0] = 0x80; stub[1] = 0x3D;
    kw_store_u32(&stub[2], (kw_u32)(uintptr_t)(g_kw_game_module + KW_RVA_ENFORCE_FPS_LIMIT_THIS_FRAME));
    stub[6] = 0x00;
    stub[7] = 0x74; stub[8] = 0x0B;
    stub[9] = 0x56;
    stub[10] = 0xE8;
    kw_store_rel32(&stub[11], stub + 15, kw_pace_client_frame);
    stub[15] = 0xE9;
    kw_store_rel32(&stub[16], stub + 20, g_kw_game_module + KW_RVA_OUTER_PACING_HISTORY);
    stub[20] = 0xE9;
    kw_store_rel32(&stub[21], stub + 25, g_kw_game_module + KW_RVA_OUTER_PACING_NO_LIMIT);
    if (!kw_finalize_executable_stub(stub, 25)) return FALSE;
    g_pacing_stub = stub;
    return TRUE;
}

static void kw_apply_from_game_hook(const char *source) {
    if (!kw_initialize_runtime_files() || g_runtime_disabled) return;
    kw_log_text("Game hook reached: ");
    kw_log_line(source);
    if (!g_static_patches_installed && !kw_install_static_patches()) {
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
    kw_u8 runtime_original[5];
    /* DllMain resolves guarded signatures and installs two five-byte redirections. */
    if (!kw_validate_game_pe_headers(g_kw_game_module)) {
        InterlockedExchange(&g_kw_bootstrap_status, KW_BOOTSTRAP_BAD_PE);
        return FALSE;
    }
    if (!kw_resolve_game_layout(g_kw_game_module)) {
        InterlockedExchange(&g_kw_bootstrap_status, KW_BOOTSTRAP_BAD_PATCH_SITES);
        return FALSE;
    }
    memcpy(runtime_original, g_kw_game_module + KW_RVA_RUNTIME_CONFIG_TAIL_CALL,
           sizeof(runtime_original));
    if (!kw_write_relative_branch(g_kw_game_module + KW_RVA_RUNTIME_CONFIG_TAIL_CALL,
                                  0xE8, kw_runtime_config_tail_hook)) {
        InterlockedExchange(&g_kw_bootstrap_status, KW_BOOTSTRAP_WRITE_FAILED);
        return FALSE;
    }
    if (!kw_write_relative_branch(g_kw_game_module + KW_RVA_START_SESSION_TAIL_JUMP,
                                  0xE9, kw_start_session_tail_hook)) {
        kw_write_protected(g_kw_game_module + KW_RVA_RUNTIME_CONFIG_TAIL_CALL,
                           runtime_original, sizeof(runtime_original));
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
        (KwRuntimeConfigTailFn)(g_kw_game_module + KW_RVA_RUNTIME_CONFIG_TAIL_TARGET);
    int result = original(original_this);
    kw_apply_from_game_hook("GameEngine_ApplyRuntimeConfiguration tail");
    return result;
}

int kw_start_session_tail_hook(void) {
    KwStartSessionTailFn original =
        (KwStartSessionTailFn)(g_kw_game_module + KW_RVA_START_SESSION_TAIL_TARGET);
    int result = original();
    kw_apply_from_game_hook("GameEngine_StartGameSession tail");
    return result;
}
