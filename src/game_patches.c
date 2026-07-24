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
/* Process-lifetime storage used as replacement targets for absolute operands. */
static float g_visual_client_step;
static float g_client_frames_per_millisecond;
static float g_camera_scroll_scale;
static float g_camera_shake_decay;
static u32 g_retail_visual_fps = 30u;
/* Fractional-rate state is reset whenever a game session starts. */
static u32 g_particle_update_accumulator;
static u32 g_w3d_time_remainder;
/* Live GlobalData camera settings may be restored between game sessions. */
static float g_retail_camera_adjust_speed;
static float g_applied_camera_adjust_speed;
static float g_retail_keyboard_rotate_speed;
static float g_applied_keyboard_rotate_speed;
static BOOL g_camera_settings_captured;

enum {
    LOGIC_FPS = 15u,
    LOGIC_PHASE_COUNT = 6u,
    W3D_RETAIL_MILLISECONDS_PER_SECOND = 990u
};

typedef u32(THISCALL *GameClientGetFrameNumberFn)(void *game_client);
typedef void(THISCALL *SubsystemUpdateFn)(void *subsystem);
typedef int(THISCALL *W3DViewScrollByFn)(void *view, const float *delta);
typedef float(THISCALL *W3DViewGetZoomFn)(void *view);
typedef int(THISCALL *W3DViewSetZoomFn)(void *view, float zoom);

typedef struct ZoomStep {
    float multiplier;
    float offset;
} ZoomStep;

static W3DViewScrollByFn g_original_scroll_by;
static ZoomStep g_zoom_in_step;
static ZoomStep g_zoom_out_step;

typedef struct PatchInstaller {
    /* A mismatch poisons the remaining install; the caller rolls back once. */
    PatchTransaction transaction;
    BOOL ok;
} PatchInstaller;

static BOOL select_camera_rate_constants(u32 target_fps) {
    /*
     * Held zoom is an affine recurrence, not a linear distance. These are the
     * exact fractional powers of the retail 30 Hz transforms:
     *
     *   zoom in:  z' = 0.96*z - 1
     *   zoom out: z' = 1.05*z + 1
     *
     * Repeating the selected step target_fps times produces exactly the same
     * one-second transform as repeating the retail step thirty times.
     */
    switch (target_fps) {
    case 45u:
        g_zoom_in_step.multiplier = 0.973152319f;
        g_zoom_in_step.offset = -0.671192018f;
        g_zoom_out_step.multiplier = 1.033061554f;
        g_zoom_out_step.offset = 0.661231083f;
        g_camera_shake_decay = 0.825481812f;
        break;
    case 60u:
        g_zoom_in_step.multiplier = 0.979795897f;
        g_zoom_in_step.offset = -0.505102572f;
        g_zoom_out_step.multiplier = 1.024695077f;
        g_zoom_out_step.offset = 0.493901532f;
        g_camera_shake_decay = 0.866025404f;
        break;
    case 75u:
        g_zoom_in_step.multiplier = 0.983803794f;
        g_zoom_in_step.offset = -0.404905142f;
        g_zoom_out_step.multiplier = 1.019707749f;
        g_zoom_out_step.offset = 0.394154980f;
        g_camera_shake_decay = 0.891301229f;
        break;
    case 90u:
        g_zoom_in_step.multiplier = 0.986484830f;
        g_zoom_in_step.offset = -0.337879257f;
        g_zoom_out_step.multiplier = 1.016396357f;
        g_zoom_out_step.offset = 0.327927136f;
        g_camera_shake_decay = 0.908560296f;
        break;
    default:
        return FALSE;
    }
    g_camera_scroll_scale = (float)g_retail_visual_fps / (float)target_fps;
    return TRUE;
}

static float NOINLINE unit_interval_nth_root(float value, u32 degree) {
    float estimate;
    u32 iteration;

    if (value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;

    /*
     * CameraAdjustSpeed is read only at session boundaries, so a compact
     * Newton solve is preferable to importing a CRT powf dependency into the
     * freestanding proxy. Keeping this helper out of line avoids duplicating
     * the loop for each rational exponent; 24 iterations also cover blend
     * values very close to either endpoint of the float interval.
     */
    estimate = 1.0f;
    for (iteration = 0; iteration < 24u; ++iteration) {
        float denominator = 1.0f;
        u32 power;
        for (power = 1u; power < degree; ++power) denominator *= estimate;
        estimate = ((float)(degree - 1u) * estimate + value / denominator) /
                   (float)degree;
    }
    return estimate;
}

static float normalize_camera_adjust_speed(float retail, u32 target_fps) {
    float retained;

    /* Values outside (0,1) are nonstandard overshoot modes; preserve them. */
    if (retail <= 0.0f || retail >= 1.0f) return retail;
    retained = 1.0f - retail;
    switch (target_fps) {
    case 45u: {
        float root = unit_interval_nth_root(retained, 3u);
        retained = root * root;
        break;
    }
    case 60u:
        retained = unit_interval_nth_root(retained, 2u);
        break;
    case 75u: {
        float root = unit_interval_nth_root(retained, 5u);
        retained = root * root;
        break;
    }
    case 90u:
        retained = unit_interval_nth_root(retained, 3u);
        break;
    default:
        return retail;
    }
    return 1.0f - retained;
}

static void *active_w3d_view(void) {
    if (g_game == NULL) return NULL;
    return *(void **)game_address(g_game, g_game->camera_input.w3d_view_pointer);
}

static int THISCALL scroll_by_at_retail_speed(void *view, const float *delta) {
    float scaled[2];

    if (g_original_scroll_by == NULL) return 0;
    if (delta == NULL) return g_original_scroll_by(view, NULL);
    /*
     * Arrow, RMB-drag, and edge scrolling all converge on W3DView::scrollBy.
     * Scaling here also makes the view's stored m_scrollAmount agree with the
     * actual world displacement used by terrain-height adjustment.
     */
    scaled[0] = delta[0] * g_camera_scroll_scale;
    scaled[1] = delta[1] * g_camera_scroll_scale;
    return g_original_scroll_by(view, scaled);
}

static int apply_held_zoom(const ZoomStep *step) {
    void *view = active_w3d_view();
    void **vtable;
    W3DViewGetZoomFn get_zoom;
    W3DViewSetZoomFn set_zoom;
    float zoom;

    if (view == NULL) return 0;
    vtable = *(void ***)view;
    get_zoom = (W3DViewGetZoomFn)vtable[W3D_VIEW_GET_ZOOM_SLOT];
    set_zoom = (W3DViewSetZoomFn)vtable[W3D_VIEW_SET_ZOOM_SLOT];
    zoom = get_zoom(view);
    return set_zoom(view, zoom * step->multiplier + step->offset);
}

static int THISCALL held_zoom_in_at_retail_speed(
    void *behavior, int context, char active, int scroll, int rotate) {
    (void)behavior;
    (void)context;
    (void)scroll;
    (void)rotate;
    return active ? apply_held_zoom(&g_zoom_in_step) : 0;
}

static int THISCALL held_zoom_out_at_retail_speed(
    void *behavior, int context, char active, int scroll, int rotate) {
    (void)behavior;
    (void)context;
    (void)scroll;
    (void)rotate;
    return active ? apply_held_zoom(&g_zoom_out_step) : 0;
}

/*
 * KW 1.02 GameEngine_DispatchLogicPhase (Steam VA 0x006560A6) batches the
 * six logic phases correctly only when clientFPS / 15 divides six. Return the
 * exclusive phase limit for every integral ratio from three through six.
 */
static u32 STDCALL get_phase_batch_limit(u32 phase, u32 ratio) {
    /* ceil(ratio * phase / 6) identifies this phase's client-frame slice. */
    u32 client_slot = (ratio * phase + LOGIC_PHASE_COUNT - 1u) / LOGIC_PHASE_COUNT;
    /* The stock loop increments EDI first, hence the exclusive +1 limit. */
    return (LOGIC_PHASE_COUNT * client_slot) / ratio + 1u;
}

/*
 * KW 1.02 GameEngine_UpdatePhaseInterpolation, Steam VA 0x0064333E.
 * Interpolation must advance once per client slice, not once per raw phase;
 * otherwise the 2-3 and 5-6 batches at 60 FPS expose the wrong visual sample.
 */
static void THISCALL update_phase_interpolation(void *engine_pointer) {
    u8 *engine = (u8 *)engine_pointer;
    u32 ratio = g_config->target_fps / LOGIC_FPS;
    u32 phase = *(u32 *)(engine + ENGINE_CURRENT_LOGIC_PHASE);
    u32 client_slot = (ratio * phase + LOGIC_PHASE_COUNT - 1u) / LOGIC_PHASE_COUNT;
    float interpolation;

    if (ratio == 3u || ratio == 6u) {
        /* Preserve bit-for-formula stock behavior for the already-valid ratios. */
        interpolation = (float)phase / (float)LOGIC_PHASE_COUNT;
    } else {
        interpolation = (float)client_slot / (float)ratio;
    }

    if (interpolation > 1.0f) interpolation = 1.0f;
    *(float *)(engine + ENGINE_LOGIC_PHASE_INTERPOLATION) = interpolation;
}

/*
 * KW 1.02 GameLogic_UpdatePhase, Steam VA 0x005EF10F, drains pending
 * Object-to-Drawable updates once after the final phase in each client slice.
 */
static BOOL STDCALL is_client_slice_end(u32 phase) {
    u32 ratio = g_config->target_fps / LOGIC_FPS;
    u32 client_slot = (ratio * phase + LOGIC_PHASE_COUNT - 1u) / LOGIC_PHASE_COUNT;
    /* floor(6 * slot / ratio) is the final phase assigned to that slice. */
    u32 end_phase = (LOGIC_PHASE_COUNT * client_slot) / ratio;
    return phase == end_phase;
}

/*
 * KW 1.02 W3DDisplay_RenderAndPresentFrame, Steam VAs 0x004A5A7C and
 * 0x004A5AB7. Retail advances 30 * 33 = 990 W3D milliseconds per nominal
 * second. The remainder also selects the 16/17 ms and 13/14 ms live-step
 * sequences used by the camera and other W3DView consumers at 60/75 FPS.
 */
static u32 STDCALL advance_w3d_time(u32 client_frame_delta) {
    const TimingLayout *timing = &g_game->timing;
    volatile u32 *accumulated = (volatile u32 *)game_address(
        g_game, timing->w3d_accumulated_time_ms);
    volatile u32 *live_step = (volatile u32 *)game_address(
        g_game, timing->w3d_milliseconds_per_frame);
    u32 current = *accumulated;
    u32 scaled;
    u32 elapsed;

    /* Fixed-point accumulator: numerator units are W3D-ms * targetFPS. */
    scaled = g_w3d_time_remainder +
             client_frame_delta * W3D_RETAIL_MILLISECONDS_PER_SECOND;
    elapsed = scaled / g_config->target_fps;
    g_w3d_time_remainder = scaled % g_config->target_fps;
    current += elapsed;
    *accumulated = current;
    /* Publish the next quotient so direct step readers share the same long-term rate. */
    *live_step = (g_w3d_time_remainder + W3D_RETAIL_MILLISECONDS_PER_SECOND) /
                 g_config->target_fps;
    return current;
}

/*
 * Map the high-rate client counter into the frame-authored 30 Hz domain.
 * The quotient/remainder form computes ceil(actual * 30 / target) without a
 * potentially overflowing full-width product. Repeated results are expected:
 * the game's existing duplicate-frame guards then skip surplus updates.
 */
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
    /* Bresenham-style rate conversion: exactly 30 calls per targetFPS frames. */
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

/*
 * Check and write are intentionally one operation: the bytes resolved during
 * DllMain are checked again immediately before mutation, catching another
 * injector or an unsupported near-match without a separate verifier path.
 */
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
    /* x86 absolute operands contain relocated virtual addresses, not RVAs. */
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
    /* Validate both the opcode and decoded original destination. */
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
        /* The tracer update has PUSH EDI between vtable load and virtual call. */
        expected = virtual_call_with_push;
        size = sizeof(virtual_call_with_push);
        replacement[0] = 0x57;
        replacement[1] = 0xE8;
        encode_rel32(&replacement[2], game_address(g_game, rva + 6u),
                     get_retail_visual_frame);
    } else {
        /* ECX already contains GameClient* and the helper returns its value in EAX. */
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

    /* These systems integrate motion continuously and therefore need 1/targetFPS. */
    patch_pointer(installer, "camera visual-step operand",
                  visual->camera_step_operand, retail_step, &g_visual_client_step);
    patch_pointer(installer, "laser visual-step operand",
                  visual->laser_step_operand, retail_step, &g_visual_client_step);
    patch_pointer(installer, "scripted-model visual-step operand",
                  visual->model_step_operand, retail_step, &g_visual_client_step);
}

static void patch_entry_jump(PatchInstaller *installer, const char *name,
                             u32 function_rva, const void *target) {
    static const u8 expected[] = { 0x80, 0x7C, 0x24, 0x08, 0x00 };
    u8 replacement[sizeof(expected)] = { 0xE9, 0, 0, 0, 0 };

    encode_rel32(&replacement[1], game_address(g_game, function_rva + 5u), target);
    patch_bytes(installer, name, function_rva,
                expected, replacement, sizeof(replacement));
}

static void install_camera_timing(PatchInstaller *installer) {
    static const u8 camera_adjust_instruction[] = {
        0xD8, 0x8F, 0x14, 0x0B, 0x00, 0x00
    };
    static const u8 shake_instruction[] = { 0xF3, 0x0F, 0x59, 0x05 };
    const CameraInputLayout *camera = &g_game->camera_input;
    void *view;
    void **vtable;
    void **scroll_entry;
    uintptr_t module_base;
    uintptr_t entry_address;
    u32 entry_rva;
    void *scroll_function;

    if (!installer->ok) return;

    /*
     * Runtime configuration is reached after W3DView construction. Resolve
     * its actual derived-class vtable rather than assuming a fixed .rdata RVA,
     * then prove that slot 0x14 still points at the signature-found scrollBy.
     */
    view = active_w3d_view();
    if (view == NULL) {
        log_line("ERROR: live W3DView pointer is null");
        installer->ok = FALSE;
        return;
    }
    vtable = *(void ***)view;
    scroll_entry = &vtable[W3D_VIEW_SCROLL_BY_SLOT];
    module_base = (uintptr_t)g_game->module;
    entry_address = (uintptr_t)scroll_entry;
    if (entry_address < module_base ||
        entry_address - module_base > g_game->pe_size_of_image - sizeof(void *)) {
        log_line("ERROR: W3DView scrollBy vtable entry is outside the game image");
        installer->ok = FALSE;
        return;
    }
    entry_rva = (u32)(entry_address - module_base);
    scroll_function = game_address(g_game, camera->scroll_by_function);
    g_original_scroll_by = (W3DViewScrollByFn)scroll_function;
    patch_pointer(installer, "W3DView scrollBy vtable entry",
                  entry_rva, scroll_function, scroll_by_at_retail_speed);

    /*
     * Only the held-key behaviors are replaced. The mouse-wheel handler keeps
     * calling the stock zoomIn/zoomOut methods once per physical wheel detent.
     */
    patch_entry_jump(installer, "held zoom-in behavior",
                     camera->zoom_in_behavior_function,
                     held_zoom_in_at_retail_speed);
    patch_entry_jump(installer, "held zoom-out behavior",
                     camera->zoom_out_behavior_function,
                     held_zoom_out_at_retail_speed);

    /* The live GlobalData fields are changed later, after all code writes succeed. */
    expect_bytes(installer, "camera height-adjust instruction",
                 camera->camera_adjust_instruction,
                 camera_adjust_instruction, sizeof(camera_adjust_instruction));

    /* Preserve the retail 30 Hz camera-shake half-life at the raised client rate. */
    expect_bytes(installer, "camera shake damping instruction",
                 camera->shake_decay_operand - sizeof(shake_instruction),
                 shake_instruction, sizeof(shake_instruction));
    patch_pointer(installer, "camera shake damping operand",
                  camera->shake_decay_operand,
                  game_address(g_game, camera->retail_shake_decay),
                  &g_camera_shake_decay);
}

static void install_frame_authored_effects(PatchInstaller *installer) {
    const VisualLayout *visual = &g_game->visual;

    /* Keep all stored and compared timestamps in the same synthetic domain. */
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

    /* Gate only CPU simulation; W3D buffer preparation remains per-client-frame. */
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

    /* Radius decal periods are milliseconds, so convert with targetFPS/1000. */
    expect_bytes(installer, "radius-cursor throb instruction",
                 visual->radius_cursor_fps_instruction,
                 radius_opcode, sizeof(radius_opcode));
    patch_pointer(installer, "radius-cursor frames-per-millisecond operand",
                  visual->radius_cursor_fps_operand,
                  game_address(g_game, visual->retail_frames_per_millisecond),
                  &g_client_frames_per_millisecond);
}

static void install_phase_scheduler(PatchInstaller *installer) {
    static const u8 batch_expected[] = {
        0x8D, 0x47, 0xFF, 0x0F, 0xAF, 0xC1, 0x99, 0x6A, 0x06, 0x5E, 0xF7,
        0xFE, 0x40, 0x6B, 0xC0, 0x06, 0x99, 0xF7, 0xF9, 0x8B, 0xF0, 0x46
    };
    static const u8 interpolation_expected[] = { 0xF3, 0x0F, 0x2A, 0x41, 0x40 };
    static const u8 flush_template[] = {
        0xA1, 0, 0, 0, 0, 0x33, 0xD2, 0xF7, 0x35, 0, 0, 0, 0,
        0x6A, 0x06, 0x8B, 0xC8, 0x58, 0x99, 0xF7, 0xF9, 0x5F, 0x8B, 0xC8,
        0x8B, 0x45, 0x08, 0x99, 0xF7, 0xF9, 0x85, 0xD2, 0x75, 0x05,
        0xE8, 0, 0, 0, 0
    };
    const SchedulerLayout *scheduler = &g_game->scheduler;
    u8 batch_patch[sizeof(batch_expected)];
    u8 interpolation_patch[sizeof(interpolation_expected)] = { 0xE9, 0, 0, 0, 0 };
    u8 flush_expected[sizeof(flush_template)];
    u8 flush_patch[sizeof(flush_template)];
    u32 batch_rva = scheduler->phase_batch_block;
    u32 interpolation_rva = scheduler->phase_interpolation_function;
    u32 flush_rva = scheduler->client_slice_flush.instruction - 34u;

    /*
     * Original register contract at KW 1.02 Steam VA 0x006560A6:
     *   EDI = first phase already dispatched, ECX = targetFPS / 15.
     * The following loop expects ESI to contain the exclusive phase limit.
     * __stdcall preserves EDI and cleans the two pushed arguments.
     */
    memset(batch_patch, 0x90, sizeof(batch_patch));
    batch_patch[0] = 0x51;
    batch_patch[1] = 0x57;
    batch_patch[2] = 0xE8;
    encode_rel32(&batch_patch[3], game_address(g_game, batch_rva + 7u),
                 get_phase_batch_limit);
    batch_patch[7] = 0x8B;
    batch_patch[8] = 0xF0;
    /* Remaining bytes are NOPs; execution rejoins at the original +22. */

    /*
     * Tail-jump from the standalone leaf entry. ECX remains GameEngine*, and
     * the helper's RET returns directly to the leaf's original caller.
     */
    encode_rel32(&interpolation_patch[1],
                 game_address(g_game, interpolation_rva + 5u),
                 update_phase_interpolation);

    memcpy(flush_expected, flush_template, sizeof(flush_expected));
    encode_u32(&flush_expected[1],
               (u32)(uintptr_t)game_address(g_game, g_game->timing.client_fps));
    encode_u32(&flush_expected[9],
               (u32)(uintptr_t)game_address(g_game, g_game->timing.logic_fps));
    encode_rel32(&flush_expected[35], game_address(g_game, flush_rva + 39u),
                 game_address(g_game, scheduler->client_slice_flush.target));

    /*
     * At KW 1.02 Steam VA 0x005EF10F, [EBP+8] is the current phase and the
     * function's saved EDI is still on the stack. The replacement asks whether
     * this phase ends the current client slice, restores EDI at the original
     * point, and conditionally calls the decoded original sub_6DE5B1 target.
     */
    memset(flush_patch, 0x90, sizeof(flush_patch));
    flush_patch[0] = 0xFF;
    flush_patch[1] = 0x75;
    flush_patch[2] = 0x08;
    flush_patch[3] = 0xE8;
    encode_rel32(&flush_patch[4], game_address(g_game, flush_rva + 8u),
                 is_client_slice_end);
    flush_patch[8] = 0x5F;
    flush_patch[9] = 0x85;
    flush_patch[10] = 0xC0;
    flush_patch[11] = 0x74;
    flush_patch[12] = 0x05;
    flush_patch[13] = 0xE8;
    encode_rel32(&flush_patch[14], game_address(g_game, flush_rva + 18u),
                 game_address(g_game, scheduler->client_slice_flush.target));
    /* The trailing NOPs rejoin at +39 with the original stack already restored. */

    patch_bytes(installer, "logic-phase batching block", batch_rva,
                batch_expected, batch_patch, sizeof(batch_patch));
    patch_bytes(installer, "logic-phase interpolation function", interpolation_rva,
                interpolation_expected, interpolation_patch,
                sizeof(interpolation_patch));
    patch_bytes(installer, "client-slice Drawable flush block", flush_rva,
                flush_expected, flush_patch, sizeof(flush_patch));
}

static void install_w3d_clock(PatchInstaller *installer) {
    static const u8 special_template[] = {
        0xA1, 0, 0, 0, 0, 0x03, 0x05, 0, 0, 0, 0, 0x50, 0xA3, 0, 0, 0, 0
    };
    static const u8 normal_template[] = {
        0xA1, 0, 0, 0, 0, 0x0F, 0xAF, 0xC6, 0x01, 0x05,
        0, 0, 0, 0, 0xFF, 0x35, 0, 0, 0, 0
    };
    const W3DClockLayout *clock = &g_game->w3d_clock;
    u32 accumulated = (u32)(uintptr_t)game_address(
        g_game, g_game->timing.w3d_accumulated_time_ms);
    u32 live_step = (u32)(uintptr_t)game_address(
        g_game, g_game->timing.w3d_milliseconds_per_frame);
    u8 special_expected[sizeof(special_template)];
    u8 normal_expected[sizeof(normal_template)];
    u8 special_patch[sizeof(special_template)];
    u8 normal_patch[sizeof(normal_template)];

    /*
     * Both overwritten blocks end immediately before an existing
     * W3D_SetCurrentTimeMs call. advance_w3d_time is __stdcall, returns the new
     * accumulated time in EAX, and cleans its one argument; PUSH EAX restores
     * the original setter's stack argument.
     */
    memcpy(special_expected, special_template, sizeof(special_expected));
    encode_u32(&special_expected[1], accumulated);
    encode_u32(&special_expected[7], live_step);
    encode_u32(&special_expected[13], accumulated);
    memset(special_patch, 0x90, sizeof(special_patch));
    special_patch[0] = 0x6A;
    special_patch[1] = 0x01;
    special_patch[2] = 0xE8;
    encode_rel32(&special_patch[3],
                 game_address(g_game, clock->special_advance_block + 7u),
                 advance_w3d_time);
    special_patch[7] = 0x50;

    /* ESI is the normal path's client-frame delta and is callee-saved. */
    memcpy(normal_expected, normal_template, sizeof(normal_expected));
    encode_u32(&normal_expected[1], live_step);
    encode_u32(&normal_expected[10], accumulated);
    encode_u32(&normal_expected[16], accumulated);
    memset(normal_patch, 0x90, sizeof(normal_patch));
    normal_patch[0] = 0x56;
    normal_patch[1] = 0xE8;
    encode_rel32(&normal_patch[2],
                 game_address(g_game, clock->normal_advance_block + 6u),
                 advance_w3d_time);
    normal_patch[6] = 0x50;

    patch_bytes(installer, "special W3D time advance", clock->special_advance_block,
                special_expected, special_patch, sizeof(special_patch));
    patch_bytes(installer, "normal W3D time advance", clock->normal_advance_block,
                normal_expected, normal_patch, sizeof(normal_patch));
}

static void install_frame_limiter(PatchInstaller *installer, u8 *pacing_stub) {
    static const u8 display_branch[] = { 0x7D, 0x13 };
    static const u8 display_patch[] = { 0xEB, 0x13 };
    static const u8 pacing_gate[] = { 0x80, 0x3D };
    static const u8 pacing_branch[] = { 0x00, 0x74, 0x69 };
    const PacingLayout *pacing = &g_game->pacing;
    u8 pacing_patch[9] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };
    u32 expected_gate_flag;

    /* Remove the independent 29 ms presentation wait but retain its timestamp update. */
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

    /* Bootstrap and session hooks can both arrive; code bytes are patched once. */
    if (g_installed) return TRUE;
    if (game == NULL || config == NULL ||
        (config->precise_pacing && pacing_stub == NULL) ||
        !select_camera_rate_constants(config->target_fps)) {
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
    install_camera_timing(&installer);
    install_frame_authored_effects(&installer);
    install_particle_and_cursor_timing(&installer);
    install_phase_scheduler(&installer);
    install_w3d_clock(&installer);
    install_frame_limiter(&installer, pacing_stub);

    /* No live timing globals have been changed yet, so rollback is complete. */
    if (!installer.ok) goto rollback;

    g_installed = TRUE;
    log_line("Static timing and limiter patches installed");
    log_line("Frame-counted particles, tracers, clouds and Anim2D pinned to retail 30 Hz");
    log_line("Six logic phases distributed across the configured client-frame rate");
    log_line("W3D time preserves the retail 990 milliseconds per nominal second");
    log_line("Radius-cursor opacity throb converted using the live client FPS");
    log_line("Camera scrolling, held zoom/rotation, settling and shake normalized");
    return TRUE;

rollback:
    log_line("ERROR: patch installation failed; rolling back static patches");
    patch_transaction_rollback(&installer.transaction);
    return FALSE;
}

static void apply_live_camera_settings(void) {
    u8 *global_data;
    float *camera_adjust;
    float *keyboard_rotate;

    if (g_game == NULL || g_config == NULL) return;
    global_data = *(u8 **)game_address(g_game, g_game->timing.global_data_pointer);
    if (global_data == NULL) return;

    camera_adjust = (float *)(global_data + GLOBAL_DATA_CAMERA_ADJUST_SPEED);
    keyboard_rotate =
        (float *)(global_data + GLOBAL_DATA_KEYBOARD_CAMERA_ROTATE_SPEED);

    /*
     * A session/configuration reload may restore authored GlobalData values.
     * Capture a changed incoming value, but do not mistake our own previously
     * applied coefficient for a new retail baseline.
     */
    if (!g_camera_settings_captured ||
        load_u32(camera_adjust) != load_u32(&g_applied_camera_adjust_speed)) {
        g_retail_camera_adjust_speed = *camera_adjust;
    }
    if (!g_camera_settings_captured ||
        load_u32(keyboard_rotate) != load_u32(&g_applied_keyboard_rotate_speed)) {
        g_retail_keyboard_rotate_speed = *keyboard_rotate;
    }

    g_applied_camera_adjust_speed = normalize_camera_adjust_speed(
        g_retail_camera_adjust_speed, g_config->target_fps);
    g_applied_keyboard_rotate_speed =
        g_retail_keyboard_rotate_speed * g_camera_scroll_scale;
    *camera_adjust = g_applied_camera_adjust_speed;
    *keyboard_rotate = g_applied_keyboard_rotate_speed;
    g_camera_settings_captured = TRUE;
}

void game_patches_reset_state(void) {
    if (g_config != NULL) {
        /* Seed so particle simulation runs on the first client frame of a session. */
        g_particle_update_accumulator = g_config->target_fps - g_retail_visual_fps;
        g_w3d_time_remainder = 0;
        /* First direct reader gets floor(990/FPS); the hook distributes remainder. */
        *(u32 *)game_address(g_game, g_game->timing.w3d_milliseconds_per_frame) =
            W3D_RETAIL_MILLISECONDS_PER_SECOND / g_config->target_fps;
        apply_live_camera_settings();
    }
}
