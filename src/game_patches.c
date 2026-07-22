#include "game_patches.h"

#include "config.h"
#include "game_layout.h"
#include "log.h"
#include "memory_patch.h"

static BOOL g_installed;
static float g_visual_client_step;
static float g_client_frames_per_millisecond;
static kw_u32 g_retail_visual_fps = 30u;
static kw_u32 g_particle_update_accumulator;

typedef kw_u32 (KW_THISCALL *KwGameClientGetFrameNumberFn)(void *game_client);
typedef void (KW_THISCALL *KwSubsystemUpdateFn)(void *subsystem);

/* Map the high-rate client counter into the frame-authored 30 Hz domain. */
static kw_u32 KW_THISCALL kw_get_retail_visual_frame(void *game_client) {
    void **vtable;
    KwGameClientGetFrameNumberFn get_frame_number;
    kw_u32 actual_frame;
    kw_u32 whole_intervals;
    kw_u32 remainder;
    kw_u32 target_fps = g_kw_config.target_fps;

    if (game_client == NULL || target_fps < g_retail_visual_fps) return 0;
    vtable = *(void ***)game_client;
    get_frame_number = (KwGameClientGetFrameNumberFn)vtable[0x78u / sizeof(void *)];
    actual_frame = get_frame_number(game_client);
    whole_intervals = actual_frame / target_fps;
    remainder = actual_frame % target_fps;
    return whole_intervals * g_retail_visual_fps +
           (remainder * g_retail_visual_fps + target_fps - 1u) / target_fps;
}

static void KW_THISCALL kw_update_particles_at_retail_rate(void *manager) {
    KwSubsystemUpdateFn update;
    kw_u32 target_fps = g_kw_config.target_fps;

    if (manager == NULL) return;
    update = (KwSubsystemUpdateFn)kw_game_address(
        g_kw_game_layout.visual.particle_simulation.target);
    if (target_fps <= g_retail_visual_fps) {
        update(manager);
        return;
    }
    g_particle_update_accumulator += g_retail_visual_fps;
    if (g_particle_update_accumulator >= target_fps) {
        g_particle_update_accumulator -= target_fps;
        update(manager);
    }
}

static BOOL kw_validate_bytes(const char *name, kw_u32 rva,
                              const kw_u8 *expected, size_t size) {
    const kw_u8 *actual = kw_game_address(rva);
    if (memcmp(actual, expected, size) == 0) return TRUE;
    kw_log_text("ERROR: runtime code mismatch at ");
    kw_log_line(name);
    kw_log_hex32("  RVA=", rva);
    if (size >= sizeof(kw_u32)) {
        kw_log_hex32("  actual first four bytes=", kw_load_u32(actual));
        kw_log_hex32("  expected first four bytes=", kw_load_u32(expected));
    }
    return FALSE;
}

static BOOL kw_validate_u32(const char *name, kw_u32 rva, kw_u32 expected) {
    kw_u32 actual = kw_load_u32(kw_game_address(rva));
    if (actual == expected) return TRUE;
    kw_log_text("ERROR: runtime value mismatch at ");
    kw_log_line(name);
    kw_log_hex32("  RVA=", rva);
    kw_log_hex32("  actual=", actual);
    kw_log_hex32("  expected=", expected);
    return FALSE;
}

static BOOL kw_validate_branch(const char *name, const KwBranchSite *branch, kw_u8 opcode) {
    kw_i32 displacement;
    kw_i32 target;
    if (*kw_game_address(branch->instruction) != opcode) {
        kw_log_text("ERROR: runtime branch opcode mismatch at ");
        kw_log_line(name);
        return FALSE;
    }
    memcpy(&displacement, kw_game_address(branch->instruction + 1u), sizeof(displacement));
    target = (kw_i32)(branch->instruction + 5u) + displacement;
    if (target >= 0 && (kw_u32)target == branch->target) return TRUE;
    kw_log_text("ERROR: runtime branch target mismatch at ");
    kw_log_line(name);
    kw_log_hex32("  actual target RVA=", (kw_u32)target);
    kw_log_hex32("  expected target RVA=", branch->target);
    return FALSE;
}

static BOOL kw_validate_patch_sites(BOOL precise_pacing) {
    static const kw_u8 display_branch[] = {0x7D, 0x13};
    static const kw_u8 virtual_frame_call[] = {0x8B, 0x01, 0xFF, 0x50, 0x78};
    static const kw_u8 tracer_update_call[] = {0x8B, 0x01, 0x57, 0xFF, 0x50, 0x78};
    static const kw_u8 gpu_particle_opcode[] = {0x0F, 0xAF, 0x05};
    static const kw_u8 radius_cursor_opcode[] = {0xD8, 0x0D};
    static const kw_u8 pacing_prefix[] = {0x80, 0x3D};
    static const kw_u8 pacing_suffix[] = {0x00, 0x74, 0x69};
    const KwTimingLayout *timing = &g_kw_game_layout.timing;
    const KwVisualLayout *visual = &g_kw_game_layout.visual;
    const KwPacingLayout *pacing = &g_kw_game_layout.pacing;
    kw_u32 retail_step = (kw_u32)(uintptr_t)kw_game_address(visual->retail_step);
    BOOL valid = TRUE;

    kw_log_u32("Incoming client FPS=", kw_load_u32(kw_game_address(timing->client_fps)));
    kw_log_u32("Incoming W3D milliseconds/client-frame=",
               kw_load_u32(kw_game_address(timing->w3d_milliseconds_per_frame)));
    kw_log_hex32("Incoming client-FPS float bits=",
                 kw_load_u32(kw_game_address(timing->client_fps_float)));
    kw_log_hex32("Incoming audio milliseconds/client-frame bits=",
                 kw_load_u32(kw_game_address(timing->audio_milliseconds_per_frame)));
    kw_log_hex32("Incoming visual seconds/client-frame bits=",
                 kw_load_u32(kw_game_address(timing->visual_seconds_per_frame)));

    if (!kw_validate_u32("g_logicFPS", timing->logic_fps, 15u)) valid = FALSE;
    if (!kw_validate_bytes("display limiter branch", pacing->display_limiter_branch,
                           display_branch, sizeof(display_branch))) valid = FALSE;
    if (!kw_validate_u32("camera visual-step operand", visual->camera_step_operand,
                         retail_step)) valid = FALSE;
    if (!kw_validate_u32("laser visual-step operand", visual->laser_step_operand,
                         retail_step)) valid = FALSE;
    if (!kw_validate_u32("scripted-model visual-step operand", visual->model_step_operand,
                         retail_step)) valid = FALSE;
    if (!kw_validate_bytes("tracer reset frame read", visual->tracer_reset_frame_call,
                           virtual_frame_call, sizeof(virtual_frame_call))) valid = FALSE;
    if (!kw_validate_bytes("tracer update frame read", visual->tracer_update_frame_call,
                           tracer_update_call, sizeof(tracer_update_call))) valid = FALSE;
    if (!kw_validate_bytes("cloud-effect frame read", visual->cloud_frame_call,
                           virtual_frame_call, sizeof(virtual_frame_call))) valid = FALSE;
    if (!kw_validate_bytes("Anim2D timestamp frame read", visual->anim2d_timestamp_frame_call,
                           virtual_frame_call, sizeof(virtual_frame_call))) valid = FALSE;
    if (!kw_validate_bytes("Anim2D update frame read", visual->anim2d_update_frame_call,
                           virtual_frame_call, sizeof(virtual_frame_call))) valid = FALSE;
    if (!kw_validate_branch("FX particle simulation call", &visual->particle_simulation,
                            0xE8)) valid = FALSE;
    if (!kw_validate_bytes("GPU particle expiry instruction",
                           visual->gpu_particle_fps_instruction,
                           gpu_particle_opcode, sizeof(gpu_particle_opcode))) valid = FALSE;
    if (!kw_validate_u32("GPU particle client-FPS operand", visual->gpu_particle_fps_operand,
                         (kw_u32)(uintptr_t)kw_game_address(timing->client_fps))) valid = FALSE;
    if (!kw_validate_bytes("radius-cursor throb frame-rate instruction",
                           visual->radius_cursor_fps_instruction,
                           radius_cursor_opcode, sizeof(radius_cursor_opcode))) valid = FALSE;
    if (!kw_validate_u32("radius-cursor frames-per-millisecond operand",
                         visual->radius_cursor_fps_operand,
                         (kw_u32)(uintptr_t)kw_game_address(
                             visual->retail_frames_per_millisecond))) valid = FALSE;

    if (precise_pacing) {
        if (!kw_validate_bytes("outer pacing gate opcode", pacing->outer_gate,
                               pacing_prefix, sizeof(pacing_prefix))) valid = FALSE;
        if (!kw_validate_u32("outer pacing gate flag operand", pacing->outer_gate + 2u,
                             (kw_u32)(uintptr_t)kw_game_address(
                                 pacing->enforce_limit_flag))) valid = FALSE;
        if (!kw_validate_bytes("outer pacing gate branch", pacing->outer_gate + 6u,
                               pacing_suffix, sizeof(pacing_suffix))) valid = FALSE;
    }
    return valid;
}

static BOOL kw_install_patch(KwPatchTransaction *transaction, kw_u32 rva,
                             const void *replacement, size_t size) {
    return kw_patch_transaction_write(
        transaction, kw_game_address(rva), replacement, size);
}

BOOL kw_game_patches_install(kw_u8 *pacing_stub) {
    static const kw_u8 display_patch[] = {0xEB, 0x13};
    KwPatchTransaction transaction;
    const KwVisualLayout *visual = &g_kw_game_layout.visual;
    const KwPacingLayout *pacing = &g_kw_game_layout.pacing;
    kw_u8 tracer_reset_patch[5] = {0xE8, 0, 0, 0, 0};
    kw_u8 tracer_update_patch[6] = {0x57, 0xE8, 0, 0, 0, 0};
    kw_u8 frame_call_patch[5] = {0xE8, 0, 0, 0, 0};
    kw_u8 particle_patch[5] = {0xE8, 0, 0, 0, 0};
    kw_u8 pacing_patch[9] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90};
    kw_u32 visual_step_pointer;
    kw_u32 reference_fps_pointer;
    kw_u32 client_frames_per_ms_pointer;

    if (g_installed) return TRUE;
    if (g_kw_config.precise_pacing && pacing_stub == NULL) return FALSE;
    if (!kw_validate_patch_sites(g_kw_config.precise_pacing)) {
        kw_log_line("ERROR: one or more runtime patch sites did not match expected bytes/state");
        return FALSE;
    }

    g_visual_client_step = 1.0f / (float)g_kw_config.target_fps;
    g_client_frames_per_millisecond = (float)g_kw_config.target_fps / 1000.0f;
    visual_step_pointer = (kw_u32)(uintptr_t)&g_visual_client_step;
    reference_fps_pointer = (kw_u32)(uintptr_t)&g_retail_visual_fps;
    client_frames_per_ms_pointer = (kw_u32)(uintptr_t)&g_client_frames_per_millisecond;
    kw_patch_transaction_init(&transaction);

    /* Per-frame visual integration uses the live seconds/client-frame value. */
    if (!kw_install_patch(&transaction, visual->camera_step_operand,
                          &visual_step_pointer, sizeof(visual_step_pointer)) ||
        !kw_install_patch(&transaction, visual->laser_step_operand,
                          &visual_step_pointer, sizeof(visual_step_pointer)) ||
        !kw_install_patch(&transaction, visual->model_step_operand,
                          &visual_step_pointer, sizeof(visual_step_pointer))) {
        goto rollback;
    }

    /* Frame-authored effects receive a synthetic 30 Hz client-frame number. */
    kw_encode_rel32(&tracer_reset_patch[1],
                    kw_game_address(visual->tracer_reset_frame_call + 5u),
                    kw_get_retail_visual_frame);
    if (!kw_install_patch(&transaction, visual->tracer_reset_frame_call,
                          tracer_reset_patch, sizeof(tracer_reset_patch))) goto rollback;

    kw_encode_rel32(&tracer_update_patch[2],
                    kw_game_address(visual->tracer_update_frame_call + 6u),
                    kw_get_retail_visual_frame);
    if (!kw_install_patch(&transaction, visual->tracer_update_frame_call,
                          tracer_update_patch, sizeof(tracer_update_patch))) goto rollback;

    kw_encode_rel32(&frame_call_patch[1],
                    kw_game_address(visual->cloud_frame_call + 5u),
                    kw_get_retail_visual_frame);
    if (!kw_install_patch(&transaction, visual->cloud_frame_call,
                          frame_call_patch, sizeof(frame_call_patch))) goto rollback;
    kw_encode_rel32(&frame_call_patch[1],
                    kw_game_address(visual->anim2d_timestamp_frame_call + 5u),
                    kw_get_retail_visual_frame);
    if (!kw_install_patch(&transaction, visual->anim2d_timestamp_frame_call,
                          frame_call_patch, sizeof(frame_call_patch))) goto rollback;
    kw_encode_rel32(&frame_call_patch[1],
                    kw_game_address(visual->anim2d_update_frame_call + 5u),
                    kw_get_retail_visual_frame);
    if (!kw_install_patch(&transaction, visual->anim2d_update_frame_call,
                          frame_call_patch, sizeof(frame_call_patch))) goto rollback;

    /* Keep particle simulation at 30 Hz and fix the two mixed timebase operands. */
    kw_encode_rel32(&particle_patch[1],
                    kw_game_address(visual->particle_simulation.instruction + 5u),
                    kw_update_particles_at_retail_rate);
    if (!kw_install_patch(&transaction, visual->particle_simulation.instruction,
                          particle_patch, sizeof(particle_patch)) ||
        !kw_install_patch(&transaction, visual->gpu_particle_fps_operand,
                          &reference_fps_pointer, sizeof(reference_fps_pointer)) ||
        !kw_install_patch(&transaction, visual->radius_cursor_fps_operand,
                          &client_frames_per_ms_pointer,
                          sizeof(client_frames_per_ms_pointer)) ||
        !kw_install_patch(&transaction, pacing->display_limiter_branch,
                          display_patch, sizeof(display_patch))) {
        goto rollback;
    }

    /* The generated stub replaces only the outer pacing gate. */
    if (g_kw_config.precise_pacing) {
        kw_encode_rel32(&pacing_patch[1], kw_game_address(pacing->outer_gate + 5u), pacing_stub);
        if (!kw_install_patch(&transaction, pacing->outer_gate,
                              pacing_patch, sizeof(pacing_patch))) goto rollback;
    }

    g_installed = TRUE;
    kw_log_line("Static visual and limiter patches installed");
    kw_log_line("Frame-counted particles, tracers, clouds and Anim2D pinned to retail 30 Hz");
    kw_log_line("Radius-cursor opacity throb converted using the live client FPS");
    return TRUE;

rollback:
    kw_log_line("ERROR: patch write failed; rolling back static patches");
    kw_patch_transaction_rollback(&transaction);
    return FALSE;
}

void kw_game_patches_reset_state(void) {
    g_particle_update_accumulator = g_kw_config.target_fps - g_retail_visual_fps;
}
