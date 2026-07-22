#include "game_1_02.h"

static BOOL kw_matches(const kw_u8 *address, const kw_u8 *expected, size_t size) {
    return memcmp(address, expected, size) == 0;
}

BOOL kw_validate_game_pe_headers(kw_u8 *module) {
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS32 *nt;
    if (module == NULL) {
        return FALSE;
    }
    dos = (IMAGE_DOS_HEADER *)module;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return FALSE;
    }
    nt = (IMAGE_NT_HEADERS32 *)(module + (kw_u32)dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE &&
           nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
           nt->FileHeader.TimeDateStamp == KW_PE_TIMESTAMP &&
           nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
           nt->OptionalHeader.AddressOfEntryPoint == KW_PE_ENTRY_RVA &&
           nt->OptionalHeader.SizeOfImage == KW_PE_SIZE_OF_IMAGE &&
           nt->OptionalHeader.CheckSum == KW_PE_CHECKSUM;
}

BOOL kw_validate_bootstrap_patch_sites(kw_u8 *module) {
    static const kw_u8 runtime_call[5] = {0xE8, 0x87, 0xBA, 0x12, 0x00};
    static const kw_u8 start_jump[5] = {0xE9, 0x86, 0x2E, 0xFF, 0xFF};
    static const kw_u8 display_branch[2] = {0x7D, 0x13};
    static const kw_u8 camera_opcode[2] = {0xD9, 0x05};
    static const kw_u8 laser_opcode[4] = {0xF3, 0x0F, 0x10, 0x1D};
    static const kw_u8 model_opcode[4] = {0xF3, 0x0F, 0x58, 0x05};
    static const kw_u8 tracer_reset_get_frame[5] = {0x8B, 0x01, 0xFF, 0x50, 0x78};
    static const kw_u8 tracer_update_get_frame[6] = {0x8B, 0x01, 0x57, 0xFF, 0x50, 0x78};
    static const kw_u8 virtual_get_frame[5] = {0x8B, 0x01, 0xFF, 0x50, 0x78};
    static const kw_u8 fx_particle_simulation_call[5] = {0xE8, 0xE4, 0x19, 0x2E, 0x00};
    static const kw_u8 gpu_particle_frame_rate[7] = {0x0F, 0xAF, 0x05, 0x80, 0xEA, 0xB7, 0x00};
    static const kw_u8 pacing_prefix[2] = {0x80, 0x3D};
    static const kw_u8 pacing_suffix[3] = {0x00, 0x74, 0x69};
    kw_u32 retail_visual_address = (kw_u32)(uintptr_t)(module + KW_RVA_RETAIL_VISUAL_STEP);
    kw_u32 pacing_flag_address =
        (kw_u32)(uintptr_t)(module + KW_RVA_ENFORCE_FPS_LIMIT_THIS_FRAME);

    if (!kw_matches(module + KW_RVA_RUNTIME_CONFIG_TAIL_CALL, runtime_call, sizeof(runtime_call)) ||
        !kw_matches(module + KW_RVA_START_SESSION_TAIL_JUMP, start_jump, sizeof(start_jump)) ||
        !kw_matches(module + KW_RVA_DISPLAY_LIMITER_BRANCH, display_branch, sizeof(display_branch)) ||
        !kw_matches(module + KW_RVA_VISUAL_STEP_CAMERA_INSTRUCTION, camera_opcode, sizeof(camera_opcode)) ||
        !kw_matches(module + KW_RVA_VISUAL_STEP_LASER_INSTRUCTION, laser_opcode, sizeof(laser_opcode)) ||
        !kw_matches(module + KW_RVA_VISUAL_STEP_MODEL_INSTRUCTION, model_opcode, sizeof(model_opcode)) ||
        !kw_matches(module + KW_RVA_TRACER_RESET_GET_FRAME, tracer_reset_get_frame,
                    sizeof(tracer_reset_get_frame)) ||
        !kw_matches(module + KW_RVA_TRACER_UPDATE_GET_FRAME, tracer_update_get_frame,
                    sizeof(tracer_update_get_frame)) ||
        !kw_matches(module + KW_RVA_CLOUD_EFFECT_GET_FRAME, virtual_get_frame,
                    sizeof(virtual_get_frame)) ||
        !kw_matches(module + KW_RVA_ANIM2D_SET_FRAME_GET_FRAME, virtual_get_frame,
                    sizeof(virtual_get_frame)) ||
        !kw_matches(module + KW_RVA_ANIM2D_UPDATE_GET_FRAME, virtual_get_frame,
                    sizeof(virtual_get_frame)) ||
        !kw_matches(module + KW_RVA_FX_PARTICLE_SIMULATION_CALL, fx_particle_simulation_call,
                    sizeof(fx_particle_simulation_call)) ||
        !kw_matches(module + KW_RVA_GPU_PARTICLE_FRAME_RATE_INSTRUCTION, gpu_particle_frame_rate,
                    sizeof(gpu_particle_frame_rate)) ||
        !kw_matches(module + KW_RVA_OUTER_PACING_GATE, pacing_prefix, sizeof(pacing_prefix)) ||
        !kw_matches(module + KW_RVA_OUTER_PACING_GATE + 6, pacing_suffix, sizeof(pacing_suffix))) {
        return FALSE;
    }

    return *(kw_u32 *)(module + KW_RVA_VISUAL_STEP_CAMERA_OPERAND) == retail_visual_address &&
           *(kw_u32 *)(module + KW_RVA_VISUAL_STEP_LASER_OPERAND) == retail_visual_address &&
           *(kw_u32 *)(module + KW_RVA_VISUAL_STEP_MODEL_OPERAND) == retail_visual_address &&
           *(kw_u32 *)(module + KW_RVA_OUTER_PACING_GATE + 2) == pacing_flag_address &&
           *(kw_u32 *)(module + KW_RVA_LOGIC_FPS) == 15u &&
           *(kw_u32 *)(module + KW_RVA_CLIENT_UPDATE_FPS) == 30u;
}
