#include "game_patches.h"

#include "log.h"
#include "memory_patch.h"

/*
 * Hook callbacks cannot carry user data, so they retain the immutable game
 * description selected during installation. Everything else stays local to
 * the installer below.
 */
static const GameLayout *g_game;
static const Config *g_config;
static BOOL g_installed;
static float g_visual_client_step;
static float g_client_frames_per_millisecond;
static u32 g_retail_visual_fps = 30u;
static u32 g_particle_update_accumulator;

typedef u32(THISCALL *GameClientGetFrameNumberFn)(void *game_client);
typedef void(THISCALL *SubsystemUpdateFn)(void *subsystem);

typedef struct PatchInstaller {
    PatchTransaction transaction;
    BOOL ok;
} PatchInstaller;

/* Map the high-rate client counter into the frame-authored 30 Hz domain. */
static u32 THISCALL get_retail_visual_frame(void *game_client) {
    void **vtable;
    GameClientGetFrameNumberFn get_frame_number;
    u32 actual_frame;
    u32 target_fps = g_config->target_fps;

    if (game_client == NULL || target_fps < g_retail_visual_fps) return 0;
    vtable = *(void ***)game_client;
    get_frame_number = (GameClientGetFrameNumberFn)vtable[0x78u / sizeof(void *)];
    actual_frame = get_frame_number(game_client);
    return (actual_frame / target_fps) * g_retail_visual_fps +
           ((actual_frame % target_fps) * g_retail_visual_fps + target_fps - 1u) /
               target_fps;
}

static void THISCALL update_particles_at_retail_rate(void *manager) {
    SubsystemUpdateFn update;
    u32 target_fps = g_config->target_fps;

    if (manager == NULL) return;
    update = (SubsystemUpdateFn)game_address(
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

static void log_mismatch(const char *name, u32 rva,
                         const void *expected, size_t size) {
    log_text("ERROR: runtime code mismatch at ");
    log_line(name);
    log_hex32("  RVA=", rva);
    if (size >= sizeof(u32)) {
        log_hex32("  actual first four bytes=", load_u32(game_address(g_game, rva)));
        log_hex32("  expected first four bytes=", load_u32(expected));
    }
}

/* Check and write are intentionally one operation: each site is described once. */
static void patch_bytes(PatchInstaller *installer, const char *name, u32 rva,
                        const void *expected, const void *replacement, size_t size) {
    void *destination = game_address(g_game, rva);
    if (!installer->ok) return;
    if (memcmp(destination, expected, size) != 0) {
        log_mismatch(name, rva, expected, size);
        installer->ok = FALSE;
        return;
    }
    if (!patch_transaction_write(
            &installer->transaction, destination, replacement, size)) {
        log_text("ERROR: could not write runtime patch at ");
        log_line(name);
        installer->ok = FALSE;
    }
}

static void expect_bytes(PatchInstaller *installer, const char *name, u32 rva,
                         const void *expected, size_t size) {
    if (!installer->ok) return;
    if (memcmp(game_address(g_game, rva), expected, size) != 0) {
        log_mismatch(name, rva, expected, size);
        installer->ok = FALSE;
    }
}

static void patch_pointer(PatchInstaller *installer, const char *name, u32 rva,
                          const void *expected_target, const void *replacement_target) {
    u32 expected = (u32)(uintptr_t)expected_target;
    u32 replacement = (u32)(uintptr_t)replacement_target;
    patch_bytes(installer, name, rva, &expected, &replacement, sizeof(replacement));
}

static void patch_branch(PatchInstaller *installer, const char *name,
                         const BranchSite *site, u8 opcode,
                         const void *replacement, size_t replacement_size) {
    i32 displacement;
    i32 actual_target;
    u8 *instruction = game_address(g_game, site->instruction);

    if (!installer->ok) return;
    if (instruction[0] == opcode) {
        memcpy(&displacement, instruction + 1u, sizeof(displacement));
        actual_target = (i32)(site->instruction + 5u) + displacement;
        if (actual_target >= 0 && (u32)actual_target == site->target) {
            if (!patch_transaction_write(
                    &installer->transaction, instruction, replacement, replacement_size)) {
                log_text("ERROR: could not write runtime patch at ");
                log_line(name);
                installer->ok = FALSE;
            }
            return;
        }
    }
    log_text("ERROR: runtime branch mismatch at ");
    log_line(name);
    log_hex32("  RVA=", site->instruction);
    installer->ok = FALSE;
}

static void redirect_frame_read(PatchInstaller *installer, const char *name,
                                u32 rva, BOOL preserves_edi) {
    static const u8 virtual_call[] = { 0x8B, 0x01, 0xFF, 0x50, 0x78 };
    static const u8 virtual_call_with_push[] = { 0x8B, 0x01, 0x57, 0xFF, 0x50, 0x78 };
    u8 replacement[6] = { 0xE8, 0, 0, 0, 0, 0 };
    const u8 *expected = virtual_call;
    size_t size = sizeof(virtual_call);

    if (preserves_edi) {
        expected = virtual_call_with_push;
        size = sizeof(virtual_call_with_push);
        replacement[0] = 0x57;
        replacement[1] = 0xE8;
        encode_rel32(&replacement[2], game_address(g_game, rva + 6u),
                     get_retail_visual_frame);
    } else {
        encode_rel32(&replacement[1], game_address(g_game, rva + 5u),
                     get_retail_visual_frame);
    }
    patch_bytes(installer, name, rva, expected, replacement, size);
}

static void log_incoming_timing(void) {
    const TimingLayout *timing = &g_game->timing;
    log_u32("Incoming client FPS=",
            load_u32(game_address(g_game, timing->client_fps)));
    log_u32("Incoming W3D milliseconds/client-frame=",
            load_u32(game_address(g_game, timing->w3d_milliseconds_per_frame)));
    log_hex32("Incoming client-FPS float bits=",
              load_u32(game_address(g_game, timing->client_fps_float)));
    log_hex32("Incoming audio milliseconds/client-frame bits=",
              load_u32(game_address(g_game, timing->audio_milliseconds_per_frame)));
    log_hex32("Incoming visual seconds/client-frame bits=",
              load_u32(game_address(g_game, timing->visual_seconds_per_frame)));
}

static void install_continuous_visuals(PatchInstaller *installer) {
    const VisualLayout *visual = &g_game->visual;
    void *retail_step = game_address(g_game, visual->retail_step);

    patch_pointer(installer, "camera visual-step operand",
                  visual->camera_step_operand, retail_step, &g_visual_client_step);
    patch_pointer(installer, "laser visual-step operand",
                  visual->laser_step_operand, retail_step, &g_visual_client_step);
    patch_pointer(installer, "scripted-model visual-step operand",
                  visual->model_step_operand, retail_step, &g_visual_client_step);
}

static void install_frame_authored_effects(PatchInstaller *installer) {
    const VisualLayout *visual = &g_game->visual;

    redirect_frame_read(installer, "tracer reset frame read",
                        visual->tracer_reset_frame_call, FALSE);
    redirect_frame_read(installer, "tracer update frame read",
                        visual->tracer_update_frame_call, TRUE);
    redirect_frame_read(installer, "cloud-effect frame read",
                        visual->cloud_frame_call, FALSE);
    redirect_frame_read(installer, "Anim2D timestamp frame read",
                        visual->anim2d_timestamp_frame_call, FALSE);
    redirect_frame_read(installer, "Anim2D update frame read",
                        visual->anim2d_update_frame_call, FALSE);
}

static void install_particle_and_cursor_timing(PatchInstaller *installer) {
    static const u8 gpu_opcode[] = { 0x0F, 0xAF, 0x05 };
    static const u8 radius_opcode[] = { 0xD8, 0x0D };
    const VisualLayout *visual = &g_game->visual;
    u8 particle_patch[5] = { 0xE8, 0, 0, 0, 0 };

    encode_rel32(&particle_patch[1],
                 game_address(g_game, visual->particle_simulation.instruction + 5u),
                 update_particles_at_retail_rate);
    patch_branch(installer, "FX particle simulation call",
                 &visual->particle_simulation, 0xE8,
                 particle_patch, sizeof(particle_patch));

    expect_bytes(installer, "GPU particle expiry instruction",
                 visual->gpu_particle_fps_instruction,
                 gpu_opcode, sizeof(gpu_opcode));
    patch_pointer(installer, "GPU particle client-FPS operand",
                  visual->gpu_particle_fps_operand,
                  game_address(g_game, g_game->timing.client_fps),
                  &g_retail_visual_fps);

    expect_bytes(installer, "radius-cursor throb instruction",
                 visual->radius_cursor_fps_instruction,
                 radius_opcode, sizeof(radius_opcode));
    patch_pointer(installer, "radius-cursor frames-per-millisecond operand",
                  visual->radius_cursor_fps_operand,
                  game_address(g_game, visual->retail_frames_per_millisecond),
                  &g_client_frames_per_millisecond);
}

static void install_frame_limiter(PatchInstaller *installer, u8 *pacing_stub) {
    static const u8 display_branch[] = { 0x7D, 0x13 };
    static const u8 display_patch[] = { 0xEB, 0x13 };
    static const u8 pacing_gate[] = { 0x80, 0x3D };
    static const u8 pacing_branch[] = { 0x00, 0x74, 0x69 };
    const PacingLayout *pacing = &g_game->pacing;
    u8 pacing_patch[9] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };
    u32 expected_gate_flag;

    patch_bytes(installer, "display limiter branch",
                pacing->display_limiter_branch,
                display_branch, display_patch, sizeof(display_patch));
    if (!g_config->precise_pacing || !installer->ok) return;

    expected_gate_flag = (u32)(uintptr_t)game_address(
        g_game, pacing->enforce_limit_flag);
    if (memcmp(game_address(g_game, pacing->outer_gate),
               pacing_gate, sizeof(pacing_gate)) != 0 ||
        load_u32(game_address(g_game, pacing->outer_gate + 2u)) !=
            expected_gate_flag ||
        memcmp(game_address(g_game, pacing->outer_gate + 6u),
               pacing_branch, sizeof(pacing_branch)) != 0) {
        log_line("ERROR: runtime code mismatch at outer pacing gate");
        installer->ok = FALSE;
        return;
    }

    encode_rel32(&pacing_patch[1],
                 game_address(g_game, pacing->outer_gate + 5u), pacing_stub);
    if (!patch_transaction_write(&installer->transaction,
                                 game_address(g_game, pacing->outer_gate),
                                 pacing_patch, sizeof(pacing_patch))) {
        log_line("ERROR: could not write outer pacing gate patch");
        installer->ok = FALSE;
    }
}

BOOL game_patches_install(const GameLayout *game, const Config *config,
                          u8 *pacing_stub) {
    PatchInstaller installer;

    if (g_installed) return TRUE;
    if (game == NULL || config == NULL || (config->precise_pacing && pacing_stub == NULL)) {
        return FALSE;
    }
    g_game = game;
    g_config = config;
    log_incoming_timing();

    g_visual_client_step = 1.0f / (float)config->target_fps;
    g_client_frames_per_millisecond = (float)config->target_fps / 1000.0f;
    patch_transaction_init(&installer.transaction);
    installer.ok = TRUE;

    install_continuous_visuals(&installer);
    install_frame_authored_effects(&installer);
    install_particle_and_cursor_timing(&installer);
    install_frame_limiter(&installer, pacing_stub);

    if (!installer.ok) goto rollback;

    g_installed = TRUE;
    log_line("Static visual and limiter patches installed");
    log_line("Frame-counted particles, tracers, clouds and Anim2D pinned to retail 30 Hz");
    log_line("Radius-cursor opacity throb converted using the live client FPS");
    return TRUE;

rollback:
    log_line("ERROR: patch installation failed; rolling back static patches");
    patch_transaction_rollback(&installer.transaction);
    return FALSE;
}

void game_patches_reset_state(void) {
    if (g_config != NULL) {
        g_particle_update_accumulator = g_config->target_fps - g_retail_visual_fps;
    }
}
