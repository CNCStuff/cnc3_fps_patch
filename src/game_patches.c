#include "game_patches.h"

#include "log.h"
#include "memory_patch.h"

/*
 * Hook callbacks cannot carry user data, so they retain the immutable game
 * description selected during installation. Everything else stays local to
 * the installer below.
 */
static const KwGameLayout *g_game;
static const KwConfig *g_config;
static BOOL g_installed;
static float g_visual_client_step;
static float g_client_frames_per_millisecond;
static kw_u32 g_retail_visual_fps = 30u;
static kw_u32 g_particle_update_accumulator;

typedef kw_u32 (KW_THISCALL *KwGameClientGetFrameNumberFn)(void *game_client);
typedef void (KW_THISCALL *KwSubsystemUpdateFn)(void *subsystem);

typedef struct KwPatchInstaller {
    KwPatchTransaction transaction;
    BOOL ok;
} KwPatchInstaller;

/* Map the high-rate client counter into the frame-authored 30 Hz domain. */
static kw_u32 KW_THISCALL kw_get_retail_visual_frame(void *game_client) {
    void **vtable;
    KwGameClientGetFrameNumberFn get_frame_number;
    kw_u32 actual_frame;
    kw_u32 target_fps = g_config->target_fps;

    if (game_client == NULL || target_fps < g_retail_visual_fps) return 0;
    vtable = *(void ***)game_client;
    get_frame_number = (KwGameClientGetFrameNumberFn)vtable[0x78u / sizeof(void *)];
    actual_frame = get_frame_number(game_client);
    return (actual_frame / target_fps) * g_retail_visual_fps +
           ((actual_frame % target_fps) * g_retail_visual_fps + target_fps - 1u) /
               target_fps;
}

static void KW_THISCALL kw_update_particles_at_retail_rate(void *manager) {
    KwSubsystemUpdateFn update;
    kw_u32 target_fps = g_config->target_fps;

    if (manager == NULL) return;
    update = (KwSubsystemUpdateFn)kw_game_address(
        g_game, g_game->visual.particle_simulation.target);
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

static void kw_log_mismatch(const char *name, kw_u32 rva,
                            const void *expected, size_t size) {
    kw_log_text("ERROR: runtime code mismatch at ");
    kw_log_line(name);
    kw_log_hex32("  RVA=", rva);
    if (size >= sizeof(kw_u32)) {
        kw_log_hex32("  actual first four bytes=", kw_load_u32(kw_game_address(g_game, rva)));
        kw_log_hex32("  expected first four bytes=", kw_load_u32(expected));
    }
}

/* Check and write are intentionally one operation: each site is described once. */
static void kw_patch_bytes(KwPatchInstaller *installer, const char *name, kw_u32 rva,
                           const void *expected, const void *replacement, size_t size) {
    void *destination = kw_game_address(g_game, rva);
    if (!installer->ok) return;
    if (memcmp(destination, expected, size) != 0) {
        kw_log_mismatch(name, rva, expected, size);
        installer->ok = FALSE;
        return;
    }
    if (!kw_patch_transaction_write(
            &installer->transaction, destination, replacement, size)) {
        kw_log_text("ERROR: could not write runtime patch at ");
        kw_log_line(name);
        installer->ok = FALSE;
    }
}

static void kw_expect_bytes(KwPatchInstaller *installer, const char *name, kw_u32 rva,
                            const void *expected, size_t size) {
    if (!installer->ok) return;
    if (memcmp(kw_game_address(g_game, rva), expected, size) != 0) {
        kw_log_mismatch(name, rva, expected, size);
        installer->ok = FALSE;
    }
}

static void kw_patch_pointer(KwPatchInstaller *installer, const char *name, kw_u32 rva,
                             const void *expected_target, const void *replacement_target) {
    kw_u32 expected = (kw_u32)(uintptr_t)expected_target;
    kw_u32 replacement = (kw_u32)(uintptr_t)replacement_target;
    kw_patch_bytes(installer, name, rva, &expected, &replacement, sizeof(replacement));
}

static void kw_patch_branch(KwPatchInstaller *installer, const char *name,
                            const KwBranchSite *site, kw_u8 opcode,
                            const void *replacement, size_t replacement_size) {
    kw_i32 displacement;
    kw_i32 actual_target;
    kw_u8 *instruction = kw_game_address(g_game, site->instruction);

    if (!installer->ok) return;
    if (instruction[0] == opcode) {
        memcpy(&displacement, instruction + 1u, sizeof(displacement));
        actual_target = (kw_i32)(site->instruction + 5u) + displacement;
        if (actual_target >= 0 && (kw_u32)actual_target == site->target) {
            if (!kw_patch_transaction_write(
                    &installer->transaction, instruction, replacement, replacement_size)) {
                kw_log_text("ERROR: could not write runtime patch at ");
                kw_log_line(name);
                installer->ok = FALSE;
            }
            return;
        }
    }
    kw_log_text("ERROR: runtime branch mismatch at ");
    kw_log_line(name);
    kw_log_hex32("  RVA=", site->instruction);
    installer->ok = FALSE;
}

static void kw_redirect_frame_read(KwPatchInstaller *installer, const char *name,
                                   kw_u32 rva, BOOL preserves_edi) {
    static const kw_u8 virtual_call[] = {0x8B, 0x01, 0xFF, 0x50, 0x78};
    static const kw_u8 virtual_call_with_push[] = {0x8B, 0x01, 0x57, 0xFF, 0x50, 0x78};
    kw_u8 replacement[6] = {0xE8, 0, 0, 0, 0, 0};
    const kw_u8 *expected = virtual_call;
    size_t size = sizeof(virtual_call);

    if (preserves_edi) {
        expected = virtual_call_with_push;
        size = sizeof(virtual_call_with_push);
        replacement[0] = 0x57;
        replacement[1] = 0xE8;
        kw_encode_rel32(&replacement[2], kw_game_address(g_game, rva + 6u),
                        kw_get_retail_visual_frame);
    } else {
        kw_encode_rel32(&replacement[1], kw_game_address(g_game, rva + 5u),
                        kw_get_retail_visual_frame);
    }
    kw_patch_bytes(installer, name, rva, expected, replacement, size);
}

static void kw_log_incoming_timing(void) {
    const KwTimingLayout *timing = &g_game->timing;
    kw_log_u32("Incoming client FPS=",
               kw_load_u32(kw_game_address(g_game, timing->client_fps)));
    kw_log_u32("Incoming W3D milliseconds/client-frame=",
               kw_load_u32(kw_game_address(g_game, timing->w3d_milliseconds_per_frame)));
    kw_log_hex32("Incoming client-FPS float bits=",
                 kw_load_u32(kw_game_address(g_game, timing->client_fps_float)));
    kw_log_hex32("Incoming audio milliseconds/client-frame bits=",
                 kw_load_u32(kw_game_address(g_game, timing->audio_milliseconds_per_frame)));
    kw_log_hex32("Incoming visual seconds/client-frame bits=",
                 kw_load_u32(kw_game_address(g_game, timing->visual_seconds_per_frame)));
}

static void kw_install_continuous_visuals(KwPatchInstaller *installer) {
    const KwVisualLayout *visual = &g_game->visual;
    void *retail_step = kw_game_address(g_game, visual->retail_step);

    kw_patch_pointer(installer, "camera visual-step operand",
                     visual->camera_step_operand, retail_step, &g_visual_client_step);
    kw_patch_pointer(installer, "laser visual-step operand",
                     visual->laser_step_operand, retail_step, &g_visual_client_step);
    kw_patch_pointer(installer, "scripted-model visual-step operand",
                     visual->model_step_operand, retail_step, &g_visual_client_step);
}

static void kw_install_frame_authored_effects(KwPatchInstaller *installer) {
    const KwVisualLayout *visual = &g_game->visual;

    kw_redirect_frame_read(installer, "tracer reset frame read",
                           visual->tracer_reset_frame_call, FALSE);
    kw_redirect_frame_read(installer, "tracer update frame read",
                           visual->tracer_update_frame_call, TRUE);
    kw_redirect_frame_read(installer, "cloud-effect frame read",
                           visual->cloud_frame_call, FALSE);
    kw_redirect_frame_read(installer, "Anim2D timestamp frame read",
                           visual->anim2d_timestamp_frame_call, FALSE);
    kw_redirect_frame_read(installer, "Anim2D update frame read",
                           visual->anim2d_update_frame_call, FALSE);
}

static void kw_install_particle_and_cursor_timing(KwPatchInstaller *installer) {
    static const kw_u8 gpu_opcode[] = {0x0F, 0xAF, 0x05};
    static const kw_u8 radius_opcode[] = {0xD8, 0x0D};
    const KwVisualLayout *visual = &g_game->visual;
    kw_u8 particle_patch[5] = {0xE8, 0, 0, 0, 0};

    kw_encode_rel32(&particle_patch[1],
                    kw_game_address(g_game, visual->particle_simulation.instruction + 5u),
                    kw_update_particles_at_retail_rate);
    kw_patch_branch(installer, "FX particle simulation call",
                    &visual->particle_simulation, 0xE8,
                    particle_patch, sizeof(particle_patch));

    kw_expect_bytes(installer, "GPU particle expiry instruction",
                    visual->gpu_particle_fps_instruction,
                    gpu_opcode, sizeof(gpu_opcode));
    kw_patch_pointer(installer, "GPU particle client-FPS operand",
                     visual->gpu_particle_fps_operand,
                     kw_game_address(g_game, g_game->timing.client_fps),
                     &g_retail_visual_fps);

    kw_expect_bytes(installer, "radius-cursor throb instruction",
                    visual->radius_cursor_fps_instruction,
                    radius_opcode, sizeof(radius_opcode));
    /*
     * Known KW builds store this cached 30/1000 value in zero-initialized data
     * and fill it during executable CRT startup. Bootstrap resolution can run
     * before that initializer; this later hook is the first safe place to
     * verify the expected 0.03f value.
     */
    if (installer->ok &&
        kw_load_u32(kw_game_address(
            g_game, visual->retail_frames_per_millisecond)) != 0x3CF5C28Fu) {
        kw_log_line("ERROR: retail frames-per-millisecond value is not 0.03");
        installer->ok = FALSE;
    }
    kw_patch_pointer(installer, "radius-cursor frames-per-millisecond operand",
                     visual->radius_cursor_fps_operand,
                     kw_game_address(g_game, visual->retail_frames_per_millisecond),
                     &g_client_frames_per_millisecond);
}

static void kw_install_frame_limiter(KwPatchInstaller *installer, kw_u8 *pacing_stub) {
    static const kw_u8 display_branch[] = {0x7D, 0x13};
    static const kw_u8 display_patch[] = {0xEB, 0x13};
    static const kw_u8 pacing_gate[] = {0x80, 0x3D};
    static const kw_u8 pacing_branch[] = {0x00, 0x74, 0x69};
    const KwPacingLayout *pacing = &g_game->pacing;
    kw_u8 pacing_patch[9] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90};
    kw_u32 expected_gate_flag;

    kw_patch_bytes(installer, "display limiter branch",
                   pacing->display_limiter_branch,
                   display_branch, display_patch, sizeof(display_patch));
    if (!g_config->precise_pacing || !installer->ok) return;

    expected_gate_flag = (kw_u32)(uintptr_t)kw_game_address(
        g_game, pacing->enforce_limit_flag);
    if (memcmp(kw_game_address(g_game, pacing->outer_gate),
               pacing_gate, sizeof(pacing_gate)) != 0 ||
        kw_load_u32(kw_game_address(g_game, pacing->outer_gate + 2u)) !=
            expected_gate_flag ||
        memcmp(kw_game_address(g_game, pacing->outer_gate + 6u),
               pacing_branch, sizeof(pacing_branch)) != 0) {
        kw_log_line("ERROR: runtime code mismatch at outer pacing gate");
        installer->ok = FALSE;
        return;
    }

    kw_encode_rel32(&pacing_patch[1],
                    kw_game_address(g_game, pacing->outer_gate + 5u), pacing_stub);
    if (!kw_patch_transaction_write(&installer->transaction,
                                    kw_game_address(g_game, pacing->outer_gate),
                                    pacing_patch, sizeof(pacing_patch))) {
        kw_log_line("ERROR: could not write outer pacing gate patch");
        installer->ok = FALSE;
    }
}

BOOL kw_game_patches_install(const KwGameLayout *game, const KwConfig *config,
                             kw_u8 *pacing_stub) {
    KwPatchInstaller installer;

    if (g_installed) return TRUE;
    if (game == NULL || config == NULL || (config->precise_pacing && pacing_stub == NULL)) {
        return FALSE;
    }
    g_game = game;
    g_config = config;
    kw_log_incoming_timing();

    g_visual_client_step = 1.0f / (float)config->target_fps;
    g_client_frames_per_millisecond = (float)config->target_fps / 1000.0f;
    kw_patch_transaction_init(&installer.transaction);
    installer.ok = TRUE;

    kw_install_continuous_visuals(&installer);
    kw_install_frame_authored_effects(&installer);
    kw_install_particle_and_cursor_timing(&installer);
    kw_install_frame_limiter(&installer, pacing_stub);

    if (!installer.ok) goto rollback;

    g_installed = TRUE;
    kw_log_line("Static visual and limiter patches installed");
    kw_log_line("Frame-counted particles, tracers, clouds and Anim2D pinned to retail 30 Hz");
    kw_log_line("Radius-cursor opacity throb converted using the live client FPS");
    return TRUE;

rollback:
    kw_log_line("ERROR: patch installation failed; rolling back static patches");
    kw_patch_transaction_rollback(&installer.transaction);
    return FALSE;
}

void kw_game_patches_reset_state(void) {
    if (g_config != NULL) {
        g_particle_update_accumulator = g_config->target_fps - g_retail_visual_fps;
    }
}
