#include "game_layout.h"

#define PATTERN_ANY 0x100u

/*
 * Signatures are deliberately kept next to the resolver below. Each one is
 * scanned once and immediately assigned to the field that consumes it; there
 * is no parallel ID enum, registry table, or untyped matches array to keep in
 * sync.
 */
static const u16 g_kw_runtime_config_pattern[] = {
    0x01, 0x88, 0x04, 0x0C, 0x00, 0x00, 0x8B, 0x0D,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xE8, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x5F, 0x5E, 0x5B, 0xC9, 0xC3
};
static const u16 g_kw_session_tail_pattern[] = {
    0xE8, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xE8, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x5F, 0x5E, 0x5B,
    0xE9, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x53, 0x55, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x14
};
/* TW's late runtime call has a different ABI, so it has a separate signature. */
static const u16 g_tw_runtime_config_pattern[] = {
    0xE8, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x8B, 0x0D, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x89, 0x81, 0x04, 0x0C, 0x00, 0x00,
    0xA1, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x8B, 0x0D, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x01, 0x88, 0x04, 0x0C, 0x00, 0x00,
    0x5F, 0x5E, 0x5B, 0xC9, 0xC3
};
static const u16 g_tw_session_tail_pattern[] = {
    0xE8, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xE8, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x5F, 0x5E, 0x5B,
    0xE9, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x55, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10
};

/* Continuous visual integration sites that incorrectly share retail 1/30. */
static const u16 g_camera_pattern[] = {
    0x80, 0xBB, 0xC8, 0x00, 0x00, 0x00, 0x00, 0x75, 0x6F,
    0xD9, 0x05, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, 0x51
};
static const u16 g_laser_pattern[] = {
    0x0F, 0x2F, 0xF1, 0xF3, 0x0F, 0x10, 0x1D,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xF3, 0x0F, 0x2A, 0xE0, 0x0F, 0x28, 0xEC, 0xF3, 0x0F, 0x59, 0xEB
};
static const u16 g_model_pattern[] = {
    0x8B, 0x01, 0xFF, 0x50, 0x78,
    0xF3, 0x0F, 0x10, 0x86, 0xF4, 0x02, 0x00, 0x00,
    0xF3, 0x0F, 0x58, 0x05,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY
};

/* Absolute client-frame consumers whose authored data remains in 30 Hz units. */
static const u16 g_tracer_reset_pattern[] = {
    0x8B, 0x01, 0xFF, 0x50, 0x78, 0x89, 0x86, 0x90, 0x00, 0x00, 0x00, 0x5E, 0xC3
};
static const u16 g_tracer_update_pattern[] = {
    0x8B, 0x01, 0x57, 0xFF, 0x50, 0x78, 0x3B, 0x86, 0x90, 0x00, 0x00, 0x00
};
static const u16 g_cloud_pattern[] = {
    0x8B, 0x01, 0xFF, 0x50, 0x78, 0x33, 0xDB, 0x32, 0xC9, 0x39, 0x46, 0x70
};
static const u16 g_anim2d_set_pattern[] = {
    0x66, 0x89, 0x46, 0x04, 0x8B, 0x0D,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x8B, 0x01, 0xFF, 0x50, 0x78, 0x89, 0x46, 0x08
};
static const u16 g_anim2d_update_pattern[] = {
    0x8B, 0x0D, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x8B, 0x01, 0xFF, 0x50, 0x78, 0x2B, 0x46, 0x08, 0x3B, 0x46, 0x18
};

/* Particle/cursor sites use a mix of client-frame and millisecond timebases. */
static const u16 g_particle_pattern[] = {
    0x8B, 0xF1, 0xE8, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x83, 0x66, 0x40, 0x00, 0x6A, 0x02, 0x8D, 0xBE, 0x8C, 0x00, 0x00, 0x00,
    0x5B, 0xFF, 0x77, 0x04, 0x8B, 0xCF
};
static const u16 g_gpu_pattern[] = {
    0xA1, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x0F, 0xAF, 0x05, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x85, 0xC0, 0x89, 0x45, 0xFC, 0x56, 0x8B, 0xF1
};
static const u16 g_radius_cursor_throb_pattern[] = {
    0x8B, 0x0D, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x8B, 0x01, 0x57, 0xFF, 0x50, 0x78, 0x8B, 0xF8, 0x8B, 0x06,
    0xD9, 0x40, 0x14, 0x51, 0xD8, 0x0D,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x51, 0xDD, 0x1C, 0x24
};

/* Independent presentation limiter and GameEngine pacing/history control flow. */
static const u16 g_display_pattern[] = {
    0x83, 0xF9, 0x1D, 0x7D, 0x13, 0xE8
};
static const u16 g_pacing_pattern[] = {
    0x3B, 0xC8, 0x76, 0x07, 0xC6, 0x05,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, 0x00,
    0x80, 0x3D, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, 0x00,
    0x74, 0x69, 0xE8, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xDB, 0x86, 0x64, 0x01, 0x00, 0x00, 0x8B, 0xD8,
    0xD8, 0x0D, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xD8, 0x3D, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY
};
static const u16 g_history_pattern[] = {
    0xFF, 0x86, 0x60, 0x01, 0x00, 0x00,
    0x83, 0xBE, 0x60, 0x01, 0x00, 0x00, 0x40,
    0x7C, 0x07, 0x83, 0xA6, 0x60, 0x01, 0x00, 0x00, 0x00
};

/*
 * Confirmed KW virtual addresses. Absolute and relative operands are the only
 * wildcarded bytes in the signatures below.
 *
 *                              phase batch  interpolation  slice flush  W3D special  W3D normal
 * KW 1.02 EA/Origin 2009       0x00583DE7   0x0057149A     0x0051C892   0x007A4F53   0x007A4F8E
 * KW 1.02 Steam 2012           0x006560A6   0x0064333E     0x005EF10F   0x004A5A7C   0x004A5AB7
 * KW 1.03                      0x005C3C61   0x005B0F6D     0x0055C5A6   0x007F0F31   0x007F0F6C
 */
static const u16 g_phase_batch_pattern[] = {
    0x8D, 0x47, 0xFF, 0x0F, 0xAF, 0xC1, 0x99, 0x6A, 0x06, 0x5E, 0xF7,
    0xFE, 0x40, 0x6B, 0xC0, 0x06, 0x99, 0xF7, 0xF9, 0x8B, 0xF0, 0x46
};
static const u16 g_phase_interpolation_pattern[] = {
    0xF3, 0x0F, 0x2A, 0x41, 0x40, 0xF3, 0x0F, 0x59, 0x05,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x0F, 0x57, 0xC9, 0x0F, 0x2F, 0xC8, 0xF3, 0x0F, 0x11, 0x41, 0x48,
    0x77, 0x0D, 0xF3, 0x0F, 0x10, 0x0D,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x0F, 0x2F, 0xC1, 0x76, 0x03, 0x0F, 0x28, 0xC1,
    0xF3, 0x0F, 0x11, 0x41, 0x48, 0xC3
};
static const u16 g_client_slice_flush_pattern[] = {
    0xA1, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x33, 0xD2, 0xF7, 0x35,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x6A, 0x06, 0x8B, 0xC8, 0x58, 0x99, 0xF7, 0xF9, 0x5F, 0x8B, 0xC8,
    0x8B, 0x45, 0x08, 0x99, 0xF7, 0xF9, 0x85, 0xD2, 0x75, 0x05,
    0xE8, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY
};
static const u16 g_w3d_special_advance_pattern[] = {
    0xA1, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x03, 0x05, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x50, 0xA3, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY
};
static const u16 g_w3d_normal_advance_pattern[] = {
    0xA1, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x0F, 0xAF, 0xC6, 0x01, 0x05,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xFF, 0x35, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY
};

/* CRT initializers prove the identities and units of timing-cache globals. */
static const u16 g_w3d_initializer_pattern[] = {
    0xB8, 0xE8, 0x03, 0x00, 0x00, 0x33, 0xD2, 0xF7, 0x35,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xA3, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, 0xC3
};
static const u16 g_audio_initializer_pattern[] = {
    0x51, 0xA1, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xDB, 0x05, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x85, 0xC0, 0x7D, 0x06, 0xD8, 0x05,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xD8, 0x3D, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xD9, 0x1D, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x59, 0xC3
};
static const u16 g_float_initializer_pattern[] = {
    0x51, 0xA1, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xDB, 0x05, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x85, 0xC0, 0x7D, 0x06, 0xD8, 0x05,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xD9, 0x1D, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x59, 0xC3
};
static const u16 g_frames_per_millisecond_initializer_pattern[] = {
    0x51, 0xA1, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xDB, 0x05, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x85, 0xC0, 0x7D, 0x06, 0xD8, 0x05,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xD8, 0x0D, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xD9, 0x1D, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0x59, 0xC3
};
static const u16 g_seconds_initializer_pattern[] = {
    0xF3, 0x0F, 0x10, 0x05,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xF3, 0x0F, 0x5E, 0x05,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY,
    0xF3, 0x0F, 0x11, 0x05,
    PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, PATTERN_ANY, 0xC3
};

static BOOL get_nt_headers(u8 *module, IMAGE_NT_HEADERS32 **out_nt) {
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS32 *nt;
    if (module == NULL || out_nt == NULL) return FALSE;
    dos = (IMAGE_DOS_HEADER *)module;
    /* Bound e_lfanew before following a pointer supplied by an unknown image. */
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) {
        return FALSE;
    }
    nt = (IMAGE_NT_HEADERS32 *)(module + (u32)dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.SizeOfImage < 0x400000u ||
        nt->OptionalHeader.SizeOfImage > 0x04000000u) {
        return FALSE;
    }
    *out_nt = nt;
    return TRUE;
}

static BOOL pattern_matches(const u8 *bytes, const u16 *pattern, size_t size) {
    size_t i;
    for (i = 0; i < size; ++i) {
        if (pattern[i] != PATTERN_ANY && bytes[i] != (u8)pattern[i]) return FALSE;
    }
    return TRUE;
}

static BOOL find_unique_signature_filtered(
    u8 *module, IMAGE_NT_HEADERS32 *nt, const u16 *pattern, size_t pattern_size,
    size_t operand_offset, u32 expected_absolute, u32 *out_rva) {
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    u32 found = 0;
    size_t matches = 0;
    u16 section_index;

    /*
     * Scan executable virtual ranges, where relocation has already fixed
     * absolute operands. Requiring exactly one match makes every signature a
     * semantic assertion rather than a best-effort version lookup.
     */
    for (section_index = 0; section_index < nt->FileHeader.NumberOfSections;
         ++section_index, ++section) {
        u32 start;
        u32 size;
        u32 offset;
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        start = section->VirtualAddress;
        size = section->Misc.VirtualSize;
        if (start >= nt->OptionalHeader.SizeOfImage ||
            size > nt->OptionalHeader.SizeOfImage - start || size < pattern_size) {
            continue;
        }
        for (offset = 0; offset <= size - (u32)pattern_size; ++offset) {
            u8 *candidate = module + start + offset;
            if (pattern_matches(candidate, pattern, pattern_size) &&
                (operand_offset == SIZE_MAX ||
                 load_u32(candidate + operand_offset) == expected_absolute)) {
                found = start + offset;
                if (++matches > 1) return FALSE;
            }
        }
    }
    if (matches != 1) return FALSE;
    *out_rva = found;
    return TRUE;
}

static BOOL find_unique_signature(u8 *module, IMAGE_NT_HEADERS32 *nt,
                                  const u16 *pattern, size_t pattern_size,
                                  u32 *out_rva) {
    return find_unique_signature_filtered(
        module, nt, pattern, pattern_size, SIZE_MAX, 0, out_rva);
}

static BOOL find_signature_using_absolute_operand(
    u8 *module, IMAGE_NT_HEADERS32 *nt, const u16 *pattern, size_t pattern_size,
    size_t input_operand_offset, u32 expected_absolute, u32 *out_rva) {
    return find_unique_signature_filtered(module, nt, pattern, pattern_size,
                                          input_operand_offset, expected_absolute, out_rva);
}

static BOOL absolute_operand_to_rva(u8 *module, u32 image_size,
                                    u32 absolute, u32 *out_rva) {
    uintptr_t base = (uintptr_t)module;
    uintptr_t address = (uintptr_t)absolute;
    /* Reject imports, heap pointers, and any operand outside the main image. */
    if (address < base || address - base >= image_size) return FALSE;
    *out_rva = (u32)(address - base);
    return TRUE;
}

static BOOL read_absolute_rva(u8 *module, u32 image_size,
                              u32 operand_rva, u32 *out_rva) {
    if (operand_rva > image_size - sizeof(u32)) return FALSE;
    return absolute_operand_to_rva(module, image_size,
                                   load_u32(module + operand_rva), out_rva);
}

static BOOL decode_relative_target(u8 *module, u32 image_size,
                                   u32 instruction_rva, u8 opcode,
                                   u32 *out_target_rva) {
    i32 displacement;
    i32 target;
    /* BranchSite targets are stored as RVAs so ASLR never leaks past resolution. */
    if (instruction_rva > image_size - 5u || module[instruction_rva] != opcode) return FALSE;
    memcpy(&displacement, module + instruction_rva + 1u, sizeof(displacement));
    target = (i32)(instruction_rva + 5u) + displacement;
    if (target < 0 || (u32)target >= image_size) return FALSE;
    *out_target_rva = (u32)target;
    return TRUE;
}

#define FIND_UNIQUE(module, nt, pattern, out_rva) \
    find_unique_signature((module), (nt), (pattern), ARRAY_COUNT(pattern), (out_rva))

static BOOL resolve_kw_bootstrap_sites(
    u8 *module, IMAGE_NT_HEADERS32 *nt, GameLayout *game) {
    u32 match;
    BranchSite runtime_config_call;
    BranchSite start_session_tail;

    if (!FIND_UNIQUE(module, nt, g_kw_runtime_config_pattern, &match)) return FALSE;
    runtime_config_call.instruction = match + 12u;
    if (!decode_relative_target(module, game->pe_size_of_image,
                                runtime_config_call.instruction, 0xE8,
                                &runtime_config_call.target)) {
        return FALSE;
    }

    if (!FIND_UNIQUE(module, nt, g_kw_session_tail_pattern, &match)) return FALSE;
    start_session_tail.instruction = match + 13u;
    if (!decode_relative_target(module, game->pe_size_of_image,
                                start_session_tail.instruction, 0xE9,
                                &start_session_tail.target)) {
        return FALSE;
    }

    game->bootstrap.runtime_config_call = runtime_config_call;
    game->bootstrap.start_session_tail = start_session_tail;
    game->bootstrap.runtime_config_hook_kind = RUNTIME_CONFIG_HOOK_THISCALL;
    game->target_name = "Kane's Wrath (signature-resolved)";
    return TRUE;
}

static BOOL resolve_tw_bootstrap_sites(
    u8 *module, IMAGE_NT_HEADERS32 *nt, GameLayout *game) {
    u32 match;
    BranchSite runtime_config_call;
    BranchSite start_session_tail;

    if (!FIND_UNIQUE(module, nt, g_tw_runtime_config_pattern, &match)) return FALSE;
    runtime_config_call.instruction = match;
    if (!decode_relative_target(module, game->pe_size_of_image,
                                runtime_config_call.instruction, 0xE8,
                                &runtime_config_call.target)) {
        return FALSE;
    }

    if (!FIND_UNIQUE(module, nt, g_tw_session_tail_pattern, &match)) return FALSE;
    start_session_tail.instruction = match + 13u;
    if (!decode_relative_target(module, game->pe_size_of_image,
                                start_session_tail.instruction, 0xE9,
                                &start_session_tail.target)) {
        return FALSE;
    }

    game->bootstrap.runtime_config_call = runtime_config_call;
    game->bootstrap.start_session_tail = start_session_tail;
    game->bootstrap.runtime_config_hook_kind = RUNTIME_CONFIG_HOOK_NOARG;
    game->target_name = "Tiberium Wars (signature-resolved)";
    return TRUE;
}

static BOOL resolve_bootstrap_sites(
    u8 *module, IMAGE_NT_HEADERS32 *nt, GameLayout *game) {
    GameLayout kw_game = *game;
    GameLayout tw_game = *game;
    BOOL kw_resolved = resolve_kw_bootstrap_sites(module, nt, &kw_game);
    BOOL tw_resolved = resolve_tw_bootstrap_sites(module, nt, &tw_game);

    /* Exactly one ABI-specific bootstrap must match; ambiguity is unsafe. */
    if (kw_resolved == tw_resolved) return FALSE;
    *game = kw_resolved ? kw_game : tw_game;
    return TRUE;
}

static BOOL resolve_visual_sites(
    u8 *module, IMAGE_NT_HEADERS32 *nt, GameLayout *game) {
    VisualLayout *visual = &game->visual;
    u32 match;
    u32 retail_step_address;

    if (!FIND_UNIQUE(module, nt, g_camera_pattern, &match)) return FALSE;
    visual->camera_step_operand = match + 11u;
    if (!FIND_UNIQUE(module, nt, g_laser_pattern, &match)) return FALSE;
    visual->laser_step_operand = match + 7u;
    if (!FIND_UNIQUE(module, nt, g_model_pattern, &match)) return FALSE;
    visual->model_step_operand = match + 17u;

    if (!FIND_UNIQUE(module, nt, g_tracer_reset_pattern, &visual->tracer_reset_frame_call) ||
        !FIND_UNIQUE(module, nt, g_tracer_update_pattern, &visual->tracer_update_frame_call) ||
        !FIND_UNIQUE(module, nt, g_cloud_pattern, &visual->cloud_frame_call)) {
        return FALSE;
    }
    if (!FIND_UNIQUE(module, nt, g_anim2d_set_pattern, &match)) return FALSE;
    visual->anim2d_timestamp_frame_call = match + 10u;
    if (!FIND_UNIQUE(module, nt, g_anim2d_update_pattern, &match)) return FALSE;
    visual->anim2d_update_frame_call = match + 6u;

    if (!FIND_UNIQUE(module, nt, g_particle_pattern, &match)) return FALSE;
    visual->particle_simulation.instruction = match + 2u;
    if (!decode_relative_target(module, game->pe_size_of_image,
                                visual->particle_simulation.instruction, 0xE8,
                                &visual->particle_simulation.target)) {
        return FALSE;
    }

    if (!FIND_UNIQUE(module, nt, g_gpu_pattern, &match)) return FALSE;
    visual->gpu_particle_fps_instruction = match + 5u;
    visual->gpu_particle_fps_operand = match + 8u;
    if (!read_absolute_rva(module, game->pe_size_of_image,
                           visual->gpu_particle_fps_operand,
                           &game->timing.client_fps) ||
        game->timing.client_fps < 4u) {
        return FALSE;
    }
    /* The two authoritative integer rates are adjacent: logic FPS then client FPS. */
    game->timing.logic_fps = game->timing.client_fps - 4u;

    if (!FIND_UNIQUE(module, nt, g_radius_cursor_throb_pattern, &match)) return FALSE;
    visual->radius_cursor_fps_instruction = match + 20u;
    visual->radius_cursor_fps_operand = match + 22u;
    if (!read_absolute_rva(module, game->pe_size_of_image,
                           visual->radius_cursor_fps_operand,
                           &visual->retail_frames_per_millisecond) ||
        !read_absolute_rva(module, game->pe_size_of_image,
                           visual->camera_step_operand, &visual->retail_step)) {
        return FALSE;
    }

    /* Camera, laser, and model must all name the same relocated retail scalar. */
    retail_step_address = (u32)(uintptr_t)(module + visual->retail_step);
    return load_u32(module + visual->laser_step_operand) == retail_step_address &&
           load_u32(module + visual->model_step_operand) == retail_step_address;
}

static BOOL resolve_pacing_sites(
    u8 *module, IMAGE_NT_HEADERS32 *nt, GameLayout *game) {
    PacingLayout *pacing = &game->pacing;
    u32 match;
    i32 no_limit_path;

    if (!FIND_UNIQUE(module, nt, g_display_pattern, &match)) return FALSE;
    pacing->display_limiter_branch = match + 3u;

    if (!FIND_UNIQUE(module, nt, g_pacing_pattern, &match)) return FALSE;
    pacing->outer_gate = match + 11u;
    if (!read_absolute_rva(module, game->pe_size_of_image, match + 13u,
                           &pacing->enforce_limit_flag) ||
        !read_absolute_rva(module, game->pe_size_of_image, match + 35u,
                           &pacing->network_scale) ||
        !read_absolute_rva(module, game->pe_size_of_image, match + 41u,
                           &pacing->milliseconds_per_logic_frame)) {
        return FALSE;
    }

    if (!FIND_UNIQUE(module, nt, g_history_pattern, &pacing->history_path)) return FALSE;
    /* Decode the original gate's short JE destination, which the stub preserves. */
    no_limit_path = (i32)(pacing->outer_gate + 9u) +
                    (int8_t)module[pacing->outer_gate + 8u];
    if (no_limit_path < 0 || (u32)no_limit_path >= game->pe_size_of_image) return FALSE;
    pacing->no_limit_path = (u32)no_limit_path;

    /* The display wait reads GlobalData; prove it is the same resolved cluster. */
    if (!read_absolute_rva(module, game->pe_size_of_image,
                           pacing->display_limiter_branch + 11u,
                           &game->timing.global_data_pointer) ||
        game->timing.global_data_pointer != pacing->enforce_limit_flag + 0x47u ||
        pacing->enforce_limit_flag < 0x27u) {
        return FALSE;
    }

    /*
     * These globals form one fixed compiler-laid-out cluster in supported
     * builds. Derive them from the signature-proven flag instead of adding
     * several weak signatures for otherwise anonymous DWORDs.
     */
    game->timing.game_engine_pointer = pacing->enforce_limit_flag + 0x27u;
    pacing->total_wait_ms = pacing->enforce_limit_flag + 0x13u;
    pacing->last_wait_ms = pacing->enforce_limit_flag + 0x17u;
    pacing->last_frame_duration_ms = pacing->enforce_limit_flag + 0x1Bu;
    pacing->previous_frame_time_ms = pacing->enforce_limit_flag + 0x1Fu;
    return TRUE;
}

static BOOL resolve_cached_timing_values(
    u8 *module, IMAGE_NT_HEADERS32 *nt, GameLayout *game) {
    TimingLayout *timing = &game->timing;
    u32 initializer;
    u32 source_address = (u32)(uintptr_t)(module + timing->client_fps);

    /* Follow startup initializers from authoritative g_clientUpdateFPS. */
    if (!FIND_UNIQUE(module, nt, g_w3d_initializer_pattern, &initializer) ||
        load_u32(module + initializer + 9u) != source_address ||
        !read_absolute_rva(module, game->pe_size_of_image, initializer + 14u,
                           &timing->w3d_milliseconds_per_frame)) {
        return FALSE;
    }
    if (!find_signature_using_absolute_operand(
            module, nt, g_float_initializer_pattern,
            ARRAY_COUNT(g_float_initializer_pattern), 2u, source_address, &initializer) ||
        load_u32(module + initializer + 8u) != source_address ||
        !read_absolute_rva(module, game->pe_size_of_image, initializer + 24u,
                           &timing->client_fps_float)) {
        return FALSE;
    }
    if (!find_signature_using_absolute_operand(
            module, nt, g_audio_initializer_pattern,
            ARRAY_COUNT(g_audio_initializer_pattern), 2u, source_address, &initializer) ||
        load_u32(module + initializer + 8u) != source_address ||
        !read_absolute_rva(module, game->pe_size_of_image, initializer + 30u,
                           &timing->audio_milliseconds_per_frame)) {
        return FALSE;
    }
    /*
     * Prove the radius operand names the FPS-derived cache by following its
     * initializer. The cache's live value depends on initialization order and
     * is intentionally not used as build identity.
     */
    if (!find_signature_using_absolute_operand(
            module, nt, g_frames_per_millisecond_initializer_pattern,
            ARRAY_COUNT(g_frames_per_millisecond_initializer_pattern),
            2u, source_address, &initializer) ||
        load_u32(module + initializer + 8u) != source_address ||
        load_u32(module + initializer + 30u) !=
            (u32)(uintptr_t)(module + game->visual.retail_frames_per_millisecond)) {
        return FALSE;
    }

    source_address = (u32)(uintptr_t)(module + timing->client_fps_float);
    return find_signature_using_absolute_operand(
               module, nt, g_seconds_initializer_pattern,
               ARRAY_COUNT(g_seconds_initializer_pattern), 12u, source_address,
               &initializer) &&
           read_absolute_rva(module, game->pe_size_of_image, initializer + 20u,
                             &timing->visual_seconds_per_frame);
}

static BOOL resolve_scheduler_sites(
    u8 *module, IMAGE_NT_HEADERS32 *nt, GameLayout *game) {
    SchedulerLayout *scheduler = &game->scheduler;
    u32 match;
    u32 client_fps_address = (u32)(uintptr_t)(module + game->timing.client_fps);
    u32 logic_fps_address = (u32)(uintptr_t)(module + game->timing.logic_fps);

    if (!FIND_UNIQUE(module, nt, g_phase_batch_pattern,
                     &scheduler->phase_batch_block) ||
        !FIND_UNIQUE(module, nt, g_phase_interpolation_pattern,
                     &scheduler->phase_interpolation_function) ||
        !FIND_UNIQUE(module, nt, g_client_slice_flush_pattern, &match) ||
        load_u32(module + match + 1u) != client_fps_address ||
        load_u32(module + match + 9u) != logic_fps_address) {
        return FALSE;
    }

    /*
     * GameLogic_UpdatePhase block layout (KW 1.02 Steam VA 0x005EF10F):
     * +1  = &g_clientUpdateFPS, +9 = &g_logicFPS, +34 = CALL sub_6DE5B1.
     * Proving both operands prevents an unrelated pair of integer divisions
     * from being accepted as the phase-end flush.
     */
    scheduler->client_slice_flush.instruction = match + 34u;
    return decode_relative_target(
        module, game->pe_size_of_image,
        scheduler->client_slice_flush.instruction, 0xE8,
        &scheduler->client_slice_flush.target);
}

static BOOL resolve_w3d_clock_sites(
    u8 *module, IMAGE_NT_HEADERS32 *nt, GameLayout *game) {
    W3DClockLayout *clock = &game->w3d_clock;
    TimingLayout *timing = &game->timing;
    u32 special;
    u32 normal;
    u32 accumulated;
    u32 expected_step = (u32)(uintptr_t)(module + timing->w3d_milliseconds_per_frame);
    u32 expected_accumulated;

    if (!FIND_UNIQUE(module, nt, g_w3d_special_advance_pattern, &special) ||
        !FIND_UNIQUE(module, nt, g_w3d_normal_advance_pattern, &normal) ||
        !read_absolute_rva(module, game->pe_size_of_image, special + 1u,
                           &accumulated)) {
        return FALSE;
    }

    /*
     * Special block: MOV EAX,[accum]; ADD EAX,[step]; PUSH EAX; MOV [accum],EAX.
     * Normal block:  MOV EAX,[step]; IMUL EAX,ESI; ADD [accum],EAX; PUSH [accum].
     * ESI is the client-frame delta in W3DDisplay_RenderAndPresentFrame.
     */
    expected_accumulated = (u32)(uintptr_t)(module + accumulated);
    if (load_u32(module + special + 7u) != expected_step ||
        load_u32(module + special + 13u) != expected_accumulated ||
        load_u32(module + normal + 1u) != expected_step ||
        load_u32(module + normal + 10u) != expected_accumulated ||
        load_u32(module + normal + 16u) != expected_accumulated) {
        return FALSE;
    }

    clock->special_advance_block = special;
    clock->normal_advance_block = normal;
    timing->w3d_accumulated_time_ms = accumulated;
    return TRUE;
}

GameResolveResult resolve_game_layout(GameLayout *out_game, u8 *module) {
    IMAGE_NT_HEADERS32 *nt;
    GameLayout game;

    if (out_game == NULL || !get_nt_headers(module, &nt)) return GAME_INVALID_PE;
    memset(&game, 0, sizeof(game));
    game.module = module;
    game.pe_timestamp = nt->FileHeader.TimeDateStamp;
    game.pe_entry_rva = nt->OptionalHeader.AddressOfEntryPoint;
    game.pe_size_of_image = nt->OptionalHeader.SizeOfImage;

    /*
     * Resolver order is intentional: later subsystems validate operands
     * against globals found by earlier ones. Timestamp and entry point are
     * diagnostic only; instruction semantics decide compatibility.
     */
    if (!resolve_bootstrap_sites(module, nt, &game) ||
        !resolve_visual_sites(module, nt, &game) ||
        !resolve_pacing_sites(module, nt, &game) ||
        !resolve_cached_timing_values(module, nt, &game) ||
        !resolve_scheduler_sites(module, nt, &game) ||
        !resolve_w3d_clock_sites(module, nt, &game) ||
        load_u32(module + game.timing.logic_fps) != 15u ||
        load_u32(module + game.timing.client_fps) != 30u ||
        module[game.pacing.display_limiter_branch] != 0x7D ||
        module[game.pacing.display_limiter_branch + 1u] != 0x13) {
        return GAME_UNSUPPORTED_BUILD;
    }

    *out_game = game;
    return GAME_RESOLVED;
}

#undef FIND_UNIQUE
