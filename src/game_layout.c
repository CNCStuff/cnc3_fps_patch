#include "game_layout.h"

#define KW_PATTERN_ANY 0x100u

/*
 * Signatures are deliberately kept next to the resolver below. Each one is
 * scanned once and immediately assigned to the field that consumes it; there
 * is no parallel ID enum, registry table, or untyped matches array to keep in
 * sync.
 */
static const kw_u16 g_kw_runtime_tail_pattern[] = {
    0x01, 0x88, 0x04, 0x0C, 0x00, 0x00, 0x8B, 0x0D,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xE8, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x5F, 0x5E, 0x5B, 0xC9, 0xC3
};
static const kw_u16 g_kw_session_tail_pattern[] = {
    0xE8, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xE8, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x5F, 0x5E, 0x5B,
    0xE9, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x53, 0x55, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x14
};
static const kw_u16 g_tw_runtime_tail_pattern[] = {
    0xE8, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x8B, 0x0D, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x89, 0x81, 0x04, 0x0C, 0x00, 0x00,
    0xA1, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x8B, 0x0D, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x01, 0x88, 0x04, 0x0C, 0x00, 0x00,
    0x5F, 0x5E, 0x5B, 0xC9, 0xC3
};
static const kw_u16 g_tw_session_tail_pattern[] = {
    0xE8, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xE8, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x5F, 0x5E, 0x5B,
    0xE9, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x55, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10
};
static const kw_u16 g_camera_pattern[] = {
    0x80, 0xBB, 0xC8, 0x00, 0x00, 0x00, 0x00, 0x75, 0x6F,
    0xD9, 0x05, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, 0x51
};
static const kw_u16 g_laser_pattern[] = {
    0x0F, 0x2F, 0xF1, 0xF3, 0x0F, 0x10, 0x1D,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xF3, 0x0F, 0x2A, 0xE0, 0x0F, 0x28, 0xEC, 0xF3, 0x0F, 0x59, 0xEB
};
static const kw_u16 g_model_pattern[] = {
    0x8B, 0x01, 0xFF, 0x50, 0x78,
    0xF3, 0x0F, 0x10, 0x86, 0xF4, 0x02, 0x00, 0x00,
    0xF3, 0x0F, 0x58, 0x05,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY
};
static const kw_u16 g_tracer_reset_pattern[] = {
    0x8B, 0x01, 0xFF, 0x50, 0x78, 0x89, 0x86, 0x90, 0x00, 0x00, 0x00, 0x5E, 0xC3
};
static const kw_u16 g_tracer_update_pattern[] = {
    0x8B, 0x01, 0x57, 0xFF, 0x50, 0x78, 0x3B, 0x86, 0x90, 0x00, 0x00, 0x00
};
static const kw_u16 g_cloud_pattern[] = {
    0x8B, 0x01, 0xFF, 0x50, 0x78, 0x33, 0xDB, 0x32, 0xC9, 0x39, 0x46, 0x70
};
static const kw_u16 g_anim2d_set_pattern[] = {
    0x66, 0x89, 0x46, 0x04, 0x8B, 0x0D,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x8B, 0x01, 0xFF, 0x50, 0x78, 0x89, 0x46, 0x08
};
static const kw_u16 g_anim2d_update_pattern[] = {
    0x8B, 0x0D, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x8B, 0x01, 0xFF, 0x50, 0x78, 0x2B, 0x46, 0x08, 0x3B, 0x46, 0x18
};
static const kw_u16 g_particle_pattern[] = {
    0x8B, 0xF1, 0xE8, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x83, 0x66, 0x40, 0x00, 0x6A, 0x02, 0x8D, 0xBE, 0x8C, 0x00, 0x00, 0x00,
    0x5B, 0xFF, 0x77, 0x04, 0x8B, 0xCF
};
static const kw_u16 g_gpu_pattern[] = {
    0xA1, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x0F, 0xAF, 0x05, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x85, 0xC0, 0x89, 0x45, 0xFC, 0x56, 0x8B, 0xF1
};
static const kw_u16 g_radius_cursor_throb_pattern[] = {
    0x8B, 0x0D, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x8B, 0x01, 0x57, 0xFF, 0x50, 0x78, 0x8B, 0xF8, 0x8B, 0x06,
    0xD9, 0x40, 0x14, 0x51, 0xD8, 0x0D,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x51, 0xDD, 0x1C, 0x24
};
static const kw_u16 g_display_pattern[] = {
    0x83, 0xF9, 0x1D, 0x7D, 0x13, 0xE8
};
static const kw_u16 g_pacing_pattern[] = {
    0x3B, 0xC8, 0x76, 0x07, 0xC6, 0x05,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, 0x00,
    0x80, 0x3D, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, 0x00,
    0x74, 0x69, 0xE8, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xDB, 0x86, 0x64, 0x01, 0x00, 0x00, 0x8B, 0xD8,
    0xD8, 0x0D, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xD8, 0x3D, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY
};
static const kw_u16 g_history_pattern[] = {
    0xFF, 0x86, 0x60, 0x01, 0x00, 0x00,
    0x83, 0xBE, 0x60, 0x01, 0x00, 0x00, 0x40,
    0x7C, 0x07, 0x83, 0xA6, 0x60, 0x01, 0x00, 0x00, 0x00
};
static const kw_u16 g_w3d_initializer_pattern[] = {
    0xB8, 0xE8, 0x03, 0x00, 0x00, 0x33, 0xD2, 0xF7, 0x35,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xA3, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, 0xC3
};
static const kw_u16 g_audio_initializer_pattern[] = {
    0x51, 0xA1, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xDB, 0x05, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x85, 0xC0, 0x7D, 0x06, 0xD8, 0x05,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xD8, 0x3D, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xD9, 0x1D, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x59, 0xC3
};
static const kw_u16 g_float_initializer_pattern[] = {
    0x51, 0xA1, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xDB, 0x05, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x85, 0xC0, 0x7D, 0x06, 0xD8, 0x05,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xD9, 0x1D, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x59, 0xC3
};
static const kw_u16 g_frames_per_millisecond_initializer_pattern[] = {
    0x51, 0xA1, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xDB, 0x05, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x85, 0xC0, 0x7D, 0x06, 0xD8, 0x05,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xD8, 0x0D, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xD9, 0x1D, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x59, 0xC3
};
static const kw_u16 g_seconds_initializer_pattern[] = {
    0xF3, 0x0F, 0x10, 0x05,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xF3, 0x0F, 0x5E, 0x05,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xF3, 0x0F, 0x11, 0x05,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, 0xC3
};

static BOOL kw_get_nt_headers(kw_u8 *module, IMAGE_NT_HEADERS32 **out_nt) {
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS32 *nt;
    if (module == NULL || out_nt == NULL) return FALSE;
    dos = (IMAGE_DOS_HEADER *)module;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) {
        return FALSE;
    }
    nt = (IMAGE_NT_HEADERS32 *)(module + (kw_u32)dos->e_lfanew);
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

static BOOL kw_pattern_matches(const kw_u8 *bytes, const kw_u16 *pattern, size_t size) {
    size_t i;
    for (i = 0; i < size; ++i) {
        if (pattern[i] != KW_PATTERN_ANY && bytes[i] != (kw_u8)pattern[i]) return FALSE;
    }
    return TRUE;
}

static BOOL kw_find_unique_signature_filtered(
    kw_u8 *module, IMAGE_NT_HEADERS32 *nt, const kw_u16 *pattern, size_t pattern_size,
    size_t operand_offset, kw_u32 expected_absolute, kw_u32 *out_rva) {
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    kw_u32 found = 0;
    size_t matches = 0;
    kw_u16 section_index;
    for (section_index = 0; section_index < nt->FileHeader.NumberOfSections;
         ++section_index, ++section) {
        kw_u32 start;
        kw_u32 size;
        kw_u32 offset;
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        start = section->VirtualAddress;
        size = section->Misc.VirtualSize;
        if (start >= nt->OptionalHeader.SizeOfImage ||
            size > nt->OptionalHeader.SizeOfImage - start || size < pattern_size) {
            continue;
        }
        for (offset = 0; offset <= size - (kw_u32)pattern_size; ++offset) {
            kw_u8 *candidate = module + start + offset;
            if (kw_pattern_matches(candidate, pattern, pattern_size) &&
                (operand_offset == SIZE_MAX ||
                 kw_load_u32(candidate + operand_offset) == expected_absolute)) {
                found = start + offset;
                if (++matches > 1) return FALSE;
            }
        }
    }
    if (matches != 1) return FALSE;
    *out_rva = found;
    return TRUE;
}

static BOOL kw_find_unique_signature(kw_u8 *module, IMAGE_NT_HEADERS32 *nt,
                                     const kw_u16 *pattern, size_t pattern_size,
                                     kw_u32 *out_rva) {
    return kw_find_unique_signature_filtered(
        module, nt, pattern, pattern_size, SIZE_MAX, 0, out_rva);
}

static BOOL kw_find_signature_using_absolute_operand(
    kw_u8 *module, IMAGE_NT_HEADERS32 *nt, const kw_u16 *pattern, size_t pattern_size,
    size_t input_operand_offset, kw_u32 expected_absolute, kw_u32 *out_rva) {
    return kw_find_unique_signature_filtered(module, nt, pattern, pattern_size,
                                             input_operand_offset, expected_absolute, out_rva);
}

static BOOL kw_absolute_operand_to_rva(kw_u8 *module, kw_u32 image_size,
                                       kw_u32 absolute, kw_u32 *out_rva) {
    uintptr_t base = (uintptr_t)module;
    uintptr_t address = (uintptr_t)absolute;
    if (address < base || address - base >= image_size) return FALSE;
    *out_rva = (kw_u32)(address - base);
    return TRUE;
}

static BOOL kw_read_absolute_rva(kw_u8 *module, kw_u32 image_size,
                                 kw_u32 operand_rva, kw_u32 *out_rva) {
    if (operand_rva > image_size - sizeof(kw_u32)) return FALSE;
    return kw_absolute_operand_to_rva(module, image_size,
                                      kw_load_u32(module + operand_rva), out_rva);
}

static BOOL kw_decode_relative_target(kw_u8 *module, kw_u32 image_size,
                                      kw_u32 instruction_rva, kw_u8 opcode,
                                      kw_u32 *out_target_rva) {
    kw_i32 displacement;
    kw_i32 target;
    if (instruction_rva > image_size - 5u || module[instruction_rva] != opcode) return FALSE;
    memcpy(&displacement, module + instruction_rva + 1u, sizeof(displacement));
    target = (kw_i32)(instruction_rva + 5u) + displacement;
    if (target < 0 || (kw_u32)target >= image_size) return FALSE;
    *out_target_rva = (kw_u32)target;
    return TRUE;
}

#define KW_FIND_UNIQUE(module, nt, pattern, out_rva) \
    kw_find_unique_signature((module), (nt), (pattern), KW_ARRAY_COUNT(pattern), (out_rva))

static BOOL kw_resolve_kw_bootstrap_sites(
    kw_u8 *module, IMAGE_NT_HEADERS32 *nt, KwGameLayout *game) {
    kw_u32 match;
    KwBranchSite runtime_config_tail;
    KwBranchSite start_session_tail;

    if (!KW_FIND_UNIQUE(module, nt, g_kw_runtime_tail_pattern, &match)) return FALSE;
    runtime_config_tail.instruction = match + 12u;
    if (!kw_decode_relative_target(module, game->pe_size_of_image,
                                   runtime_config_tail.instruction, 0xE8,
                                   &runtime_config_tail.target)) {
        return FALSE;
    }

    if (!KW_FIND_UNIQUE(module, nt, g_kw_session_tail_pattern, &match)) return FALSE;
    start_session_tail.instruction = match + 13u;
    if (!kw_decode_relative_target(module, game->pe_size_of_image,
                                   start_session_tail.instruction, 0xE9,
                                   &start_session_tail.target)) {
        return FALSE;
    }

    game->bootstrap.runtime_config_tail = runtime_config_tail;
    game->bootstrap.start_session_tail = start_session_tail;
    game->bootstrap.runtime_config_hook_kind = KW_RUNTIME_CONFIG_HOOK_THISCALL;
    game->build_name = "Kane's Wrath (signature-resolved)";
    return TRUE;
}

static BOOL kw_resolve_tw_bootstrap_sites(
    kw_u8 *module, IMAGE_NT_HEADERS32 *nt, KwGameLayout *game) {
    kw_u32 match;
    KwBranchSite runtime_config_tail;
    KwBranchSite start_session_tail;

    if (!KW_FIND_UNIQUE(module, nt, g_tw_runtime_tail_pattern, &match)) return FALSE;
    runtime_config_tail.instruction = match;
    if (!kw_decode_relative_target(module, game->pe_size_of_image,
                                   runtime_config_tail.instruction, 0xE8,
                                   &runtime_config_tail.target)) {
        return FALSE;
    }

    if (!KW_FIND_UNIQUE(module, nt, g_tw_session_tail_pattern, &match)) return FALSE;
    start_session_tail.instruction = match + 13u;
    if (!kw_decode_relative_target(module, game->pe_size_of_image,
                                   start_session_tail.instruction, 0xE9,
                                   &start_session_tail.target)) {
        return FALSE;
    }

    game->bootstrap.runtime_config_tail = runtime_config_tail;
    game->bootstrap.start_session_tail = start_session_tail;
    game->bootstrap.runtime_config_hook_kind = KW_RUNTIME_CONFIG_HOOK_NOARG;
    game->build_name = "Tiberium Wars (signature-resolved)";
    return TRUE;
}

static BOOL kw_resolve_bootstrap_sites(
    kw_u8 *module, IMAGE_NT_HEADERS32 *nt, KwGameLayout *game) {
    KwGameLayout kw_game = *game;
    KwGameLayout tw_game = *game;
    BOOL kw_resolved = kw_resolve_kw_bootstrap_sites(module, nt, &kw_game);
    BOOL tw_resolved = kw_resolve_tw_bootstrap_sites(module, nt, &tw_game);

    if (kw_resolved == tw_resolved) return FALSE;
    *game = kw_resolved ? kw_game : tw_game;
    return TRUE;
}

static BOOL kw_resolve_visual_sites(
    kw_u8 *module, IMAGE_NT_HEADERS32 *nt, KwGameLayout *game) {
    KwVisualLayout *visual = &game->visual;
    kw_u32 match;
    kw_u32 retail_step_address;

    if (!KW_FIND_UNIQUE(module, nt, g_camera_pattern, &match)) return FALSE;
    visual->camera_step_operand = match + 11u;
    if (!KW_FIND_UNIQUE(module, nt, g_laser_pattern, &match)) return FALSE;
    visual->laser_step_operand = match + 7u;
    if (!KW_FIND_UNIQUE(module, nt, g_model_pattern, &match)) return FALSE;
    visual->model_step_operand = match + 17u;

    if (!KW_FIND_UNIQUE(module, nt, g_tracer_reset_pattern, &visual->tracer_reset_frame_call) ||
        !KW_FIND_UNIQUE(module, nt, g_tracer_update_pattern, &visual->tracer_update_frame_call) ||
        !KW_FIND_UNIQUE(module, nt, g_cloud_pattern, &visual->cloud_frame_call)) {
        return FALSE;
    }
    if (!KW_FIND_UNIQUE(module, nt, g_anim2d_set_pattern, &match)) return FALSE;
    visual->anim2d_timestamp_frame_call = match + 10u;
    if (!KW_FIND_UNIQUE(module, nt, g_anim2d_update_pattern, &match)) return FALSE;
    visual->anim2d_update_frame_call = match + 6u;

    if (!KW_FIND_UNIQUE(module, nt, g_particle_pattern, &match)) return FALSE;
    visual->particle_simulation.instruction = match + 2u;
    if (!kw_decode_relative_target(module, game->pe_size_of_image,
                                   visual->particle_simulation.instruction, 0xE8,
                                   &visual->particle_simulation.target)) {
        return FALSE;
    }

    if (!KW_FIND_UNIQUE(module, nt, g_gpu_pattern, &match)) return FALSE;
    visual->gpu_particle_fps_instruction = match + 5u;
    visual->gpu_particle_fps_operand = match + 8u;
    if (!kw_read_absolute_rva(module, game->pe_size_of_image,
                              visual->gpu_particle_fps_operand,
                              &game->timing.client_fps) ||
        game->timing.client_fps < 4u) {
        return FALSE;
    }
    game->timing.logic_fps = game->timing.client_fps - 4u;

    if (!KW_FIND_UNIQUE(module, nt, g_radius_cursor_throb_pattern, &match)) return FALSE;
    visual->radius_cursor_fps_instruction = match + 20u;
    visual->radius_cursor_fps_operand = match + 22u;
    if (!kw_read_absolute_rva(module, game->pe_size_of_image,
                              visual->radius_cursor_fps_operand,
                              &visual->retail_frames_per_millisecond) ||
        !kw_read_absolute_rva(module, game->pe_size_of_image,
                              visual->camera_step_operand, &visual->retail_step)) {
        return FALSE;
    }

    retail_step_address = (kw_u32)(uintptr_t)(module + visual->retail_step);
    return kw_load_u32(module + visual->laser_step_operand) == retail_step_address &&
           kw_load_u32(module + visual->model_step_operand) == retail_step_address;
}

static BOOL kw_resolve_pacing_sites(
    kw_u8 *module, IMAGE_NT_HEADERS32 *nt, KwGameLayout *game) {
    KwPacingLayout *pacing = &game->pacing;
    kw_u32 match;
    kw_i32 no_limit_path;

    if (!KW_FIND_UNIQUE(module, nt, g_display_pattern, &match)) return FALSE;
    pacing->display_limiter_branch = match + 3u;

    if (!KW_FIND_UNIQUE(module, nt, g_pacing_pattern, &match)) return FALSE;
    pacing->outer_gate = match + 11u;
    if (!kw_read_absolute_rva(module, game->pe_size_of_image, match + 13u,
                              &pacing->enforce_limit_flag) ||
        !kw_read_absolute_rva(module, game->pe_size_of_image, match + 35u,
                              &pacing->network_scale) ||
        !kw_read_absolute_rva(module, game->pe_size_of_image, match + 41u,
                              &pacing->milliseconds_per_logic_frame)) {
        return FALSE;
    }

    if (!KW_FIND_UNIQUE(module, nt, g_history_pattern, &pacing->history_path)) return FALSE;
    no_limit_path = (kw_i32)(pacing->outer_gate + 9u) +
                    (int8_t)module[pacing->outer_gate + 8u];
    if (no_limit_path < 0 || (kw_u32)no_limit_path >= game->pe_size_of_image) return FALSE;
    pacing->no_limit_path = (kw_u32)no_limit_path;

    if (!kw_read_absolute_rva(module, game->pe_size_of_image,
                              pacing->display_limiter_branch + 11u,
                              &game->timing.global_data_pointer) ||
        game->timing.global_data_pointer != pacing->enforce_limit_flag + 0x47u ||
        pacing->enforce_limit_flag < 0x27u) {
        return FALSE;
    }

    game->timing.game_engine_pointer = pacing->enforce_limit_flag + 0x27u;
    pacing->total_wait_ms = pacing->enforce_limit_flag + 0x13u;
    pacing->last_wait_ms = pacing->enforce_limit_flag + 0x17u;
    pacing->last_frame_duration_ms = pacing->enforce_limit_flag + 0x1Bu;
    pacing->previous_frame_time_ms = pacing->enforce_limit_flag + 0x1Fu;
    return TRUE;
}

static BOOL kw_resolve_cached_timing_values(
    kw_u8 *module, IMAGE_NT_HEADERS32 *nt, KwGameLayout *game) {
    KwTimingLayout *timing = &game->timing;
    kw_u32 initializer;
    kw_u32 source_address = (kw_u32)(uintptr_t)(module + timing->client_fps);

    if (!KW_FIND_UNIQUE(module, nt, g_w3d_initializer_pattern, &initializer) ||
        kw_load_u32(module + initializer + 9u) != source_address ||
        !kw_read_absolute_rva(module, game->pe_size_of_image, initializer + 14u,
                              &timing->w3d_milliseconds_per_frame)) {
        return FALSE;
    }
    if (!kw_find_signature_using_absolute_operand(
            module, nt, g_float_initializer_pattern,
            KW_ARRAY_COUNT(g_float_initializer_pattern), 2u, source_address, &initializer) ||
        kw_load_u32(module + initializer + 8u) != source_address ||
        !kw_read_absolute_rva(module, game->pe_size_of_image, initializer + 24u,
                              &timing->client_fps_float)) {
        return FALSE;
    }
    if (!kw_find_signature_using_absolute_operand(
            module, nt, g_audio_initializer_pattern,
            KW_ARRAY_COUNT(g_audio_initializer_pattern), 2u, source_address, &initializer) ||
        kw_load_u32(module + initializer + 8u) != source_address ||
        !kw_read_absolute_rva(module, game->pe_size_of_image, initializer + 30u,
                              &timing->audio_milliseconds_per_frame)) {
        return FALSE;
    }
    /*
     * Prove the radius operand names the FPS-derived cache by following its
     * initializer. The cache's live value depends on initialization order and
     * is intentionally not used as build identity.
     */
    if (!kw_find_signature_using_absolute_operand(
            module, nt, g_frames_per_millisecond_initializer_pattern,
            KW_ARRAY_COUNT(g_frames_per_millisecond_initializer_pattern),
            2u, source_address, &initializer) ||
        kw_load_u32(module + initializer + 8u) != source_address ||
        kw_load_u32(module + initializer + 30u) !=
            (kw_u32)(uintptr_t)(module + game->visual.retail_frames_per_millisecond)) {
        return FALSE;
    }

    source_address = (kw_u32)(uintptr_t)(module + timing->client_fps_float);
    return kw_find_signature_using_absolute_operand(
               module, nt, g_seconds_initializer_pattern,
               KW_ARRAY_COUNT(g_seconds_initializer_pattern), 12u, source_address,
               &initializer) &&
           kw_read_absolute_rva(module, game->pe_size_of_image, initializer + 20u,
                                &timing->visual_seconds_per_frame);
}

KwGameResolveResult kw_resolve_game_layout(KwGameLayout *out_game, kw_u8 *module) {
    IMAGE_NT_HEADERS32 *nt;
    KwGameLayout game;

    if (out_game == NULL || !kw_get_nt_headers(module, &nt)) return KW_GAME_INVALID_PE;
    memset(&game, 0, sizeof(game));
    game.module = module;
    game.pe_timestamp = nt->FileHeader.TimeDateStamp;
    game.pe_entry_rva = nt->OptionalHeader.AddressOfEntryPoint;
    game.pe_size_of_image = nt->OptionalHeader.SizeOfImage;

    if (!kw_resolve_bootstrap_sites(module, nt, &game) ||
        !kw_resolve_visual_sites(module, nt, &game) ||
        !kw_resolve_pacing_sites(module, nt, &game) ||
        !kw_resolve_cached_timing_values(module, nt, &game) ||
        kw_load_u32(module + game.timing.logic_fps) != 15u ||
        kw_load_u32(module + game.timing.client_fps) != 30u ||
        module[game.pacing.display_limiter_branch] != 0x7D ||
        module[game.pacing.display_limiter_branch + 1u] != 0x13) {
        return KW_GAME_UNSUPPORTED_BUILD;
    }

    *out_game = game;
    return KW_GAME_RESOLVED;
}

#undef KW_FIND_UNIQUE
