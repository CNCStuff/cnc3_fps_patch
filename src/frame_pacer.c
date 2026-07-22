#include "frame_pacer.h"

#include "config.h"
#include "game_layout.h"
#include "memory_patch.h"

static LARGE_INTEGER g_qpc_frequency;
static LARGE_INTEGER g_next_deadline;
static BOOL g_armed;
static kw_u8 *g_stub;

static void KW_STDCALL kw_pace_client_frame(void *engine_pointer) {
    const KwPacingLayout *pacing = &g_kw_game_layout.pacing;
    kw_u8 *engine = (kw_u8 *)engine_pointer;
    DWORD before_ms = timeGetTime();
    DWORD previous_ms = *(volatile DWORD *)kw_game_address(pacing->previous_frame_time_ms);
    DWORD work_ms = before_ms - previous_ms;
    kw_i32 multiplier = *(volatile kw_i32 *)(engine + KW_ENGINE_PACING_UPDATE_MULTIPLIER);
    float scale = *(volatile float *)kw_game_address(pacing->network_scale);
    float logic_ms = *(volatile float *)kw_game_address(pacing->milliseconds_per_logic_frame);
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
    if (!g_armed) {
        g_next_deadline = now;
        g_armed = TRUE;
    }
    g_next_deadline.QuadPart += period_ticks;
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
    *(volatile DWORD *)kw_game_address(pacing->last_frame_duration_ms) = work_ms;
    *(volatile DWORD *)kw_game_address(pacing->last_wait_ms) = wait_ms;
    *(volatile DWORD *)kw_game_address(pacing->total_wait_ms) += wait_ms;
    *(volatile DWORD *)kw_game_address(pacing->previous_frame_time_ms) = after_ms;
}

static BOOL kw_build_stub(void) {
    const KwPacingLayout *pacing = &g_kw_game_layout.pacing;
    kw_u8 *stub;
    if (g_stub != NULL) return TRUE;
    if (!kw_allocate_executable_stub(32, &stub)) return FALSE;

    /* Preserve both branches of the game's outer limiter gate. */
    stub[0] = 0x80; stub[1] = 0x3D;
    kw_encode_u32(&stub[2],
                  (kw_u32)(uintptr_t)kw_game_address(pacing->enforce_limit_flag));
    stub[6] = 0x00;
    stub[7] = 0x74; stub[8] = 0x0B;
    stub[9] = 0x56;
    stub[10] = 0xE8;
    kw_encode_rel32(&stub[11], stub + 15, kw_pace_client_frame);
    stub[15] = 0xE9;
    kw_encode_rel32(&stub[16], stub + 20, kw_game_address(pacing->history_path));
    stub[20] = 0xE9;
    kw_encode_rel32(&stub[21], stub + 25, kw_game_address(pacing->no_limit_path));
    if (!kw_finalize_executable_stub(stub, 25)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        return FALSE;
    }
    g_stub = stub;
    return TRUE;
}

BOOL kw_frame_pacer_initialize(BOOL enabled) {
    if (!enabled) return TRUE;
    if (!QueryPerformanceFrequency(&g_qpc_frequency) ||
        g_qpc_frequency.HighPart != 0 || g_qpc_frequency.LowPart == 0) {
        return FALSE;
    }
    return kw_build_stub();
}

kw_u8 *kw_frame_pacer_stub(void) {
    return g_stub;
}

void kw_frame_pacer_reset(void) {
    g_armed = FALSE;
    g_next_deadline.QuadPart = 0;
}
