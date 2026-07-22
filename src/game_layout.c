#include "game_layout.h"

#define KW_PATTERN_ANY 0x100u

KwGameLayout g_kw_game_layout;

static const kw_u16 g_runtime_tail_pattern[] = {
    0x01, 0x88, 0x04, 0x0C, 0x00, 0x00, 0x8B, 0x0D,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xE8, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x5F, 0x5E, 0x5B, 0xC9, 0xC3
};
static const kw_u16 g_session_tail_pattern[] = {
    0xE8, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xE8, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x5F, 0x5E, 0x5B,
    0xE9, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0x53, 0x55, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x14
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
static const kw_u16 g_seconds_initializer_pattern[] = {
    0xF3, 0x0F, 0x10, 0x05,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xF3, 0x0F, 0x5E, 0x05,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xF3, 0x0F, 0x11, 0x05,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, 0xC3
};

static kw_u32 kw_read_u32(const kw_u8 *address) {
    kw_u32 value;
    memcpy(&value, address, sizeof(value));
    return value;
}

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

BOOL kw_validate_game_pe_headers(kw_u8 *module) {
    IMAGE_NT_HEADERS32 *nt;
    return kw_get_nt_headers(module, &nt);
}

static BOOL kw_pattern_matches(const kw_u8 *bytes, const kw_u16 *pattern, size_t size) {
    size_t i;
    for (i = 0; i < size; ++i) {
        if (pattern[i] != KW_PATTERN_ANY && bytes[i] != (kw_u8)pattern[i]) return FALSE;
    }
    return TRUE;
}

static BOOL kw_find_unique_signature(kw_u8 *module, IMAGE_NT_HEADERS32 *nt,
                                     const kw_u16 *pattern, size_t pattern_size,
                                     kw_u32 *out_rva) {
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
            if (kw_pattern_matches(module + start + offset, pattern, pattern_size)) {
                found = start + offset;
                if (++matches > 1) return FALSE;
            }
        }
    }
    if (matches != 1) return FALSE;
    *out_rva = found;
    return TRUE;
}

static BOOL kw_find_signature_using_absolute_operand(
    kw_u8 *module, IMAGE_NT_HEADERS32 *nt, const kw_u16 *pattern, size_t pattern_size,
    size_t input_operand_offset, kw_u32 expected_absolute, kw_u32 *out_rva) {
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
                kw_read_u32(candidate + input_operand_offset) == expected_absolute) {
                found = start + offset;
                if (++matches > 1) return FALSE;
            }
        }
    }
    if (matches != 1) return FALSE;
    *out_rva = found;
    return TRUE;
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
                                      kw_read_u32(module + operand_rva), out_rva);
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

BOOL kw_resolve_game_layout(kw_u8 *module) {
    IMAGE_NT_HEADERS32 *nt;
    KwGameLayout layout;
    kw_u32 runtime_match, session_match, camera_match, laser_match, model_match;
    kw_u32 anim_set_match, anim_update_match, particle_match, gpu_match;
    kw_u32 display_match, pacing_match, history_match, initializer_match;
    kw_u32 absolute;
    kw_i32 short_target;
    if (!kw_get_nt_headers(module, &nt)) return FALSE;
    memset(&layout, 0, sizeof(layout));
    layout.build_name = "Kane's Wrath signature-compatible build";
    layout.pe_timestamp = nt->FileHeader.TimeDateStamp;
    layout.pe_entry_rva = nt->OptionalHeader.AddressOfEntryPoint;
    layout.pe_size_of_image = nt->OptionalHeader.SizeOfImage;

    if (!kw_find_unique_signature(module, nt, g_runtime_tail_pattern,
                                  KW_ARRAY_COUNT(g_runtime_tail_pattern), &runtime_match) ||
        !kw_find_unique_signature(module, nt, g_session_tail_pattern,
                                  KW_ARRAY_COUNT(g_session_tail_pattern), &session_match) ||
        !kw_find_unique_signature(module, nt, g_camera_pattern,
                                  KW_ARRAY_COUNT(g_camera_pattern), &camera_match) ||
        !kw_find_unique_signature(module, nt, g_laser_pattern,
                                  KW_ARRAY_COUNT(g_laser_pattern), &laser_match) ||
        !kw_find_unique_signature(module, nt, g_model_pattern,
                                  KW_ARRAY_COUNT(g_model_pattern), &model_match) ||
        !kw_find_unique_signature(module, nt, g_tracer_reset_pattern,
                                  KW_ARRAY_COUNT(g_tracer_reset_pattern),
                                  &layout.tracer_reset_get_frame) ||
        !kw_find_unique_signature(module, nt, g_tracer_update_pattern,
                                  KW_ARRAY_COUNT(g_tracer_update_pattern),
                                  &layout.tracer_update_get_frame) ||
        !kw_find_unique_signature(module, nt, g_cloud_pattern,
                                  KW_ARRAY_COUNT(g_cloud_pattern),
                                  &layout.cloud_effect_get_frame) ||
        !kw_find_unique_signature(module, nt, g_anim2d_set_pattern,
                                  KW_ARRAY_COUNT(g_anim2d_set_pattern), &anim_set_match) ||
        !kw_find_unique_signature(module, nt, g_anim2d_update_pattern,
                                  KW_ARRAY_COUNT(g_anim2d_update_pattern), &anim_update_match) ||
        !kw_find_unique_signature(module, nt, g_particle_pattern,
                                  KW_ARRAY_COUNT(g_particle_pattern), &particle_match) ||
        !kw_find_unique_signature(module, nt, g_gpu_pattern,
                                  KW_ARRAY_COUNT(g_gpu_pattern), &gpu_match) ||
        !kw_find_unique_signature(module, nt, g_display_pattern,
                                  KW_ARRAY_COUNT(g_display_pattern), &display_match) ||
        !kw_find_unique_signature(module, nt, g_pacing_pattern,
                                  KW_ARRAY_COUNT(g_pacing_pattern), &pacing_match) ||
        !kw_find_unique_signature(module, nt, g_history_pattern,
                                  KW_ARRAY_COUNT(g_history_pattern), &history_match)) {
        return FALSE;
    }

    layout.runtime_config_tail_call = runtime_match + 12u;
    layout.start_session_tail_jump = session_match + 13u;
    layout.visual_step_camera_operand = camera_match + 11u;
    layout.visual_step_laser_operand = laser_match + 7u;
    layout.visual_step_model_operand = model_match + 17u;
    layout.anim2d_set_frame_get_frame = anim_set_match + 10u;
    layout.anim2d_update_get_frame = anim_update_match + 6u;
    layout.fx_particle_simulation_call = particle_match + 2u;
    layout.gpu_particle_frame_rate_instruction = gpu_match + 5u;
    layout.gpu_particle_frame_rate_operand = gpu_match + 8u;
    layout.display_limiter_branch = display_match + 3u;
    layout.outer_pacing_gate = pacing_match + 11u;
    layout.outer_pacing_history = history_match;

    if (!kw_decode_relative_target(module, layout.pe_size_of_image,
                                   layout.runtime_config_tail_call, 0xE8,
                                   &layout.runtime_config_tail_target) ||
        !kw_decode_relative_target(module, layout.pe_size_of_image,
                                   layout.start_session_tail_jump, 0xE9,
                                   &layout.start_session_tail_target) ||
        !kw_decode_relative_target(module, layout.pe_size_of_image,
                                   layout.fx_particle_simulation_call, 0xE8,
                                   &layout.fx_particle_simulation_target)) {
        return FALSE;
    }

    if (!kw_read_absolute_rva(module, layout.pe_size_of_image,
                              layout.gpu_particle_frame_rate_operand,
                              &layout.client_update_fps) ||
        layout.client_update_fps < 4u) {
        return FALSE;
    }
    layout.logic_fps = layout.client_update_fps - 4u;
    absolute = (kw_u32)(uintptr_t)(module + layout.client_update_fps);

    if (!kw_read_absolute_rva(module, layout.pe_size_of_image,
                              layout.visual_step_camera_operand,
                              &layout.retail_visual_step) ||
        kw_read_u32(module + layout.visual_step_laser_operand) !=
            (kw_u32)(uintptr_t)(module + layout.retail_visual_step) ||
        kw_read_u32(module + layout.visual_step_model_operand) !=
            (kw_u32)(uintptr_t)(module + layout.retail_visual_step)) {
        return FALSE;
    }

    if (!kw_read_absolute_rva(module, layout.pe_size_of_image, pacing_match + 13u,
                              &layout.enforce_fps_limit_this_frame) ||
        !kw_read_absolute_rva(module, layout.pe_size_of_image, pacing_match + 35u,
                              &layout.network_frame_pacing_scale) ||
        !kw_read_absolute_rva(module, layout.pe_size_of_image, pacing_match + 41u,
                              &layout.milliseconds_per_logic_frame)) {
        return FALSE;
    }
    short_target = (kw_i32)(layout.outer_pacing_gate + 9u) +
                   (int8_t)module[layout.outer_pacing_gate + 8u];
    if (short_target < 0 || (kw_u32)short_target >= layout.pe_size_of_image) return FALSE;
    layout.outer_pacing_no_limit = (kw_u32)short_target;

    if (!kw_read_absolute_rva(module, layout.pe_size_of_image,
                              layout.display_limiter_branch + 11u,
                              &layout.global_data_pointer)) {
        return FALSE;
    }
    if (layout.global_data_pointer != layout.enforce_fps_limit_this_frame + 0x47u ||
        layout.enforce_fps_limit_this_frame < 0x27u) {
        return FALSE;
    }
    layout.game_engine_pointer = layout.enforce_fps_limit_this_frame + 0x27u;
    layout.total_limiter_wait_ms = layout.enforce_fps_limit_this_frame + 0x13u;
    layout.last_limiter_wait_ms = layout.enforce_fps_limit_this_frame + 0x17u;
    layout.last_engine_frame_duration_ms = layout.enforce_fps_limit_this_frame + 0x1Bu;
    layout.previous_engine_frame_time_ms = layout.enforce_fps_limit_this_frame + 0x1Fu;

    if (!kw_find_unique_signature(module, nt, g_w3d_initializer_pattern,
                                  KW_ARRAY_COUNT(g_w3d_initializer_pattern),
                                  &initializer_match) ||
        kw_read_u32(module + initializer_match + 9u) != absolute ||
        !kw_read_absolute_rva(module, layout.pe_size_of_image, initializer_match + 14u,
                              &layout.w3d_milliseconds_per_client_frame)) {
        return FALSE;
    }

    if (!kw_find_signature_using_absolute_operand(
            module, nt, g_float_initializer_pattern,
            KW_ARRAY_COUNT(g_float_initializer_pattern), 2u, absolute,
            &initializer_match) ||
        kw_read_u32(module + initializer_match + 8u) != absolute ||
        !kw_read_absolute_rva(module, layout.pe_size_of_image, initializer_match + 24u,
                              &layout.client_fps_float)) {
        return FALSE;
    }

    if (!kw_find_signature_using_absolute_operand(
            module, nt, g_audio_initializer_pattern,
            KW_ARRAY_COUNT(g_audio_initializer_pattern), 2u, absolute,
            &initializer_match) ||
        kw_read_u32(module + initializer_match + 8u) != absolute ||
        !kw_read_absolute_rva(module, layout.pe_size_of_image, initializer_match + 30u,
                              &layout.audio_milliseconds_per_client_frame)) {
        return FALSE;
    }

    absolute = (kw_u32)(uintptr_t)(module + layout.client_fps_float);
    if (!kw_find_signature_using_absolute_operand(
            module, nt, g_seconds_initializer_pattern,
            KW_ARRAY_COUNT(g_seconds_initializer_pattern), 12u, absolute,
            &initializer_match) ||
        !kw_read_absolute_rva(module, layout.pe_size_of_image, initializer_match + 20u,
                              &layout.visual_seconds_per_client_frame)) {
        return FALSE;
    }

    if (kw_read_u32(module + layout.logic_fps) != 15u ||
        kw_read_u32(module + layout.client_update_fps) != 30u ||
        module[layout.display_limiter_branch] != 0x7D ||
        module[layout.display_limiter_branch + 1u] != 0x13) {
        return FALSE;
    }

    g_kw_game_layout = layout;
    return TRUE;
}
