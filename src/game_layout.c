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
static const kw_u16 g_seconds_initializer_pattern[] = {
    0xF3, 0x0F, 0x10, 0x05,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xF3, 0x0F, 0x5E, 0x05,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY,
    0xF3, 0x0F, 0x11, 0x05,
    KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, KW_PATTERN_ANY, 0xC3
};

typedef enum KwSignatureId {
    KW_SIG_RUNTIME_TAIL,
    KW_SIG_SESSION_TAIL,
    KW_SIG_CAMERA,
    KW_SIG_LASER,
    KW_SIG_MODEL,
    KW_SIG_TRACER_RESET,
    KW_SIG_TRACER_UPDATE,
    KW_SIG_CLOUD,
    KW_SIG_ANIM2D_TIMESTAMP,
    KW_SIG_ANIM2D_UPDATE,
    KW_SIG_PARTICLE,
    KW_SIG_GPU_PARTICLE,
    KW_SIG_RADIUS_CURSOR,
    KW_SIG_DISPLAY,
    KW_SIG_PACING,
    KW_SIG_HISTORY,
    KW_SIG_COUNT
} KwSignatureId;

typedef struct KwSignatureSpec {
    const kw_u16 *pattern;
    size_t size;
} KwSignatureSpec;

#define KW_SIGNATURE(pattern_name) {pattern_name, KW_ARRAY_COUNT(pattern_name)}

static const KwSignatureSpec g_core_signatures[KW_SIG_COUNT] = {
    [KW_SIG_RUNTIME_TAIL] = KW_SIGNATURE(g_runtime_tail_pattern),
    [KW_SIG_SESSION_TAIL] = KW_SIGNATURE(g_session_tail_pattern),
    [KW_SIG_CAMERA] = KW_SIGNATURE(g_camera_pattern),
    [KW_SIG_LASER] = KW_SIGNATURE(g_laser_pattern),
    [KW_SIG_MODEL] = KW_SIGNATURE(g_model_pattern),
    [KW_SIG_TRACER_RESET] = KW_SIGNATURE(g_tracer_reset_pattern),
    [KW_SIG_TRACER_UPDATE] = KW_SIGNATURE(g_tracer_update_pattern),
    [KW_SIG_CLOUD] = KW_SIGNATURE(g_cloud_pattern),
    [KW_SIG_ANIM2D_TIMESTAMP] = KW_SIGNATURE(g_anim2d_set_pattern),
    [KW_SIG_ANIM2D_UPDATE] = KW_SIGNATURE(g_anim2d_update_pattern),
    [KW_SIG_PARTICLE] = KW_SIGNATURE(g_particle_pattern),
    [KW_SIG_GPU_PARTICLE] = KW_SIGNATURE(g_gpu_pattern),
    [KW_SIG_RADIUS_CURSOR] = KW_SIGNATURE(g_radius_cursor_throb_pattern),
    [KW_SIG_DISPLAY] = KW_SIGNATURE(g_display_pattern),
    [KW_SIG_PACING] = KW_SIGNATURE(g_pacing_pattern),
    [KW_SIG_HISTORY] = KW_SIGNATURE(g_history_pattern)
};

#undef KW_SIGNATURE

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

BOOL kw_resolve_game_layout(kw_u8 *module) {
    IMAGE_NT_HEADERS32 *nt;
    KwGameLayout layout;
    kw_u32 matches[KW_SIG_COUNT];
    kw_u32 initializer;
    kw_u32 absolute;
    kw_i32 short_target;
    size_t i;

    if (!kw_get_nt_headers(module, &nt)) return FALSE;
    memset(&layout, 0, sizeof(layout));
    layout.build_name = "Kane's Wrath (signature-resolved)";
    layout.pe_timestamp = nt->FileHeader.TimeDateStamp;
    layout.pe_entry_rva = nt->OptionalHeader.AddressOfEntryPoint;
    layout.pe_size_of_image = nt->OptionalHeader.SizeOfImage;

    for (i = 0; i < KW_SIG_COUNT; ++i) {
        const KwSignatureSpec *signature = &g_core_signatures[i];
        if (!kw_find_unique_signature(module, nt, signature->pattern,
                                      signature->size, &matches[i])) {
            return FALSE;
        }
    }

    layout.bootstrap.runtime_config_tail.instruction = matches[KW_SIG_RUNTIME_TAIL] + 12u;
    layout.bootstrap.start_session_tail.instruction = matches[KW_SIG_SESSION_TAIL] + 13u;

    layout.visual.camera_step_operand = matches[KW_SIG_CAMERA] + 11u;
    layout.visual.laser_step_operand = matches[KW_SIG_LASER] + 7u;
    layout.visual.model_step_operand = matches[KW_SIG_MODEL] + 17u;
    layout.visual.tracer_reset_frame_call = matches[KW_SIG_TRACER_RESET];
    layout.visual.tracer_update_frame_call = matches[KW_SIG_TRACER_UPDATE];
    layout.visual.cloud_frame_call = matches[KW_SIG_CLOUD];
    layout.visual.anim2d_timestamp_frame_call = matches[KW_SIG_ANIM2D_TIMESTAMP] + 10u;
    layout.visual.anim2d_update_frame_call = matches[KW_SIG_ANIM2D_UPDATE] + 6u;
    layout.visual.particle_simulation.instruction = matches[KW_SIG_PARTICLE] + 2u;
    layout.visual.gpu_particle_fps_instruction = matches[KW_SIG_GPU_PARTICLE] + 5u;
    layout.visual.gpu_particle_fps_operand = matches[KW_SIG_GPU_PARTICLE] + 8u;
    layout.visual.radius_cursor_fps_instruction = matches[KW_SIG_RADIUS_CURSOR] + 20u;
    layout.visual.radius_cursor_fps_operand = matches[KW_SIG_RADIUS_CURSOR] + 22u;

    layout.pacing.display_limiter_branch = matches[KW_SIG_DISPLAY] + 3u;
    layout.pacing.outer_gate = matches[KW_SIG_PACING] + 11u;
    layout.pacing.history_path = matches[KW_SIG_HISTORY];

    if (!kw_decode_relative_target(module, layout.pe_size_of_image,
                                   layout.bootstrap.runtime_config_tail.instruction, 0xE8,
                                   &layout.bootstrap.runtime_config_tail.target) ||
        !kw_decode_relative_target(module, layout.pe_size_of_image,
                                   layout.bootstrap.start_session_tail.instruction, 0xE9,
                                   &layout.bootstrap.start_session_tail.target) ||
        !kw_decode_relative_target(module, layout.pe_size_of_image,
                                   layout.visual.particle_simulation.instruction, 0xE8,
                                   &layout.visual.particle_simulation.target)) {
        return FALSE;
    }

    if (!kw_read_absolute_rva(module, layout.pe_size_of_image,
                              layout.visual.gpu_particle_fps_operand,
                              &layout.timing.client_fps) ||
        layout.timing.client_fps < 4u) {
        return FALSE;
    }
    layout.timing.logic_fps = layout.timing.client_fps - 4u;
    absolute = (kw_u32)(uintptr_t)(module + layout.timing.client_fps);

    if (!kw_read_absolute_rva(module, layout.pe_size_of_image,
                              layout.visual.radius_cursor_fps_operand,
                              &layout.visual.retail_frames_per_millisecond) ||
        kw_load_u32(module + layout.visual.retail_frames_per_millisecond) != 0x3CF5C28Fu ||
        !kw_read_absolute_rva(module, layout.pe_size_of_image,
                              layout.visual.camera_step_operand,
                              &layout.visual.retail_step) ||
        kw_load_u32(module + layout.visual.laser_step_operand) !=
            (kw_u32)(uintptr_t)(module + layout.visual.retail_step) ||
        kw_load_u32(module + layout.visual.model_step_operand) !=
            (kw_u32)(uintptr_t)(module + layout.visual.retail_step)) {
        return FALSE;
    }

    if (!kw_read_absolute_rva(module, layout.pe_size_of_image,
                              matches[KW_SIG_PACING] + 13u,
                              &layout.pacing.enforce_limit_flag) ||
        !kw_read_absolute_rva(module, layout.pe_size_of_image,
                              matches[KW_SIG_PACING] + 35u,
                              &layout.pacing.network_scale) ||
        !kw_read_absolute_rva(module, layout.pe_size_of_image,
                              matches[KW_SIG_PACING] + 41u,
                              &layout.pacing.milliseconds_per_logic_frame)) {
        return FALSE;
    }
    short_target = (kw_i32)(layout.pacing.outer_gate + 9u) +
                   (int8_t)module[layout.pacing.outer_gate + 8u];
    if (short_target < 0 || (kw_u32)short_target >= layout.pe_size_of_image) return FALSE;
    layout.pacing.no_limit_path = (kw_u32)short_target;

    if (!kw_read_absolute_rva(module, layout.pe_size_of_image,
                              layout.pacing.display_limiter_branch + 11u,
                              &layout.timing.global_data_pointer) ||
        layout.timing.global_data_pointer != layout.pacing.enforce_limit_flag + 0x47u ||
        layout.pacing.enforce_limit_flag < 0x27u) {
        return FALSE;
    }
    layout.timing.game_engine_pointer = layout.pacing.enforce_limit_flag + 0x27u;
    layout.pacing.total_wait_ms = layout.pacing.enforce_limit_flag + 0x13u;
    layout.pacing.last_wait_ms = layout.pacing.enforce_limit_flag + 0x17u;
    layout.pacing.last_frame_duration_ms = layout.pacing.enforce_limit_flag + 0x1Bu;
    layout.pacing.previous_frame_time_ms = layout.pacing.enforce_limit_flag + 0x1Fu;

    if (!kw_find_unique_signature(module, nt, g_w3d_initializer_pattern,
                                  KW_ARRAY_COUNT(g_w3d_initializer_pattern), &initializer) ||
        kw_load_u32(module + initializer + 9u) != absolute ||
        !kw_read_absolute_rva(module, layout.pe_size_of_image, initializer + 14u,
                              &layout.timing.w3d_milliseconds_per_frame)) {
        return FALSE;
    }
    if (!kw_find_signature_using_absolute_operand(
            module, nt, g_float_initializer_pattern,
            KW_ARRAY_COUNT(g_float_initializer_pattern), 2u, absolute, &initializer) ||
        kw_load_u32(module + initializer + 8u) != absolute ||
        !kw_read_absolute_rva(module, layout.pe_size_of_image, initializer + 24u,
                              &layout.timing.client_fps_float)) {
        return FALSE;
    }
    if (!kw_find_signature_using_absolute_operand(
            module, nt, g_audio_initializer_pattern,
            KW_ARRAY_COUNT(g_audio_initializer_pattern), 2u, absolute, &initializer) ||
        kw_load_u32(module + initializer + 8u) != absolute ||
        !kw_read_absolute_rva(module, layout.pe_size_of_image, initializer + 30u,
                              &layout.timing.audio_milliseconds_per_frame)) {
        return FALSE;
    }

    absolute = (kw_u32)(uintptr_t)(module + layout.timing.client_fps_float);
    if (!kw_find_signature_using_absolute_operand(
            module, nt, g_seconds_initializer_pattern,
            KW_ARRAY_COUNT(g_seconds_initializer_pattern), 12u, absolute, &initializer) ||
        !kw_read_absolute_rva(module, layout.pe_size_of_image, initializer + 20u,
                              &layout.timing.visual_seconds_per_frame)) {
        return FALSE;
    }

    if (kw_load_u32(module + layout.timing.logic_fps) != 15u ||
        kw_load_u32(module + layout.timing.client_fps) != 30u ||
        module[layout.pacing.display_limiter_branch] != 0x7D ||
        module[layout.pacing.display_limiter_branch + 1u] != 0x13) {
        return FALSE;
    }

    g_kw_game_layout = layout;
    return TRUE;
}
