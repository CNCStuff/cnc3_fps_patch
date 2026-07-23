#include "frame_pacer.h"

#include "memory_patch.h"

static const GameLayout *g_game;
static const Config *g_config;
static LARGE_INTEGER g_qpc_frequency;
static LARGE_INTEGER g_next_deadline;
static BOOL g_armed;
static u8 *g_stub;

static void STDCALL pace_client_frame(void *engine_pointer) {
    const PacingLayout *pacing = &g_game->pacing;
    u8 *engine = (u8 *)engine_pointer;
    DWORD before_ms = timeGetTime();
    DWORD previous_ms = *(volatile DWORD *)game_address(
        g_game, pacing->previous_frame_time_ms);
    DWORD work_ms = before_ms - previous_ms;
    i32 multiplier = *(volatile i32 *)(engine + ENGINE_PACING_UPDATE_MULTIPLIER);
    float scale = *(volatile float *)game_address(g_game, pacing->network_scale);
    float logic_ms = *(volatile float *)game_address(
        g_game, pacing->milliseconds_per_logic_frame);
    float period_ticks_f;
    u32 period_ticks;
    u32 spin_ticks;
    u32 one_ms_ticks;
    LARGE_INTEGER now;
    DWORD after_ms;
    DWORD wait_ms;

    if (multiplier < 1) multiplier = 1;
    if (!(scale >= 0.5f)) scale = 0.5f;
    if (scale > 1.0f) scale = 1.0f;
    period_ticks_f = ((float)g_qpc_frequency.LowPart * logic_ms) /
                     (1000.0f * (float)multiplier * scale);
    period_ticks = (u32)(period_ticks_f + 0.5f);
    if (period_ticks == 0) period_ticks = 1;
    one_ms_ticks = g_qpc_frequency.LowPart / 1000u;
    spin_ticks = (one_ms_ticks * g_config->spin_threshold_us) / 1000u;

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
        if ((u64)remaining > (u64)spin_ticks + (u64)one_ms_ticks * 2u) {
            Sleep(1);
        } else if ((u64)remaining > spin_ticks) {
            if (!SwitchToThread()) Sleep(0);
        }
    }

    after_ms = timeGetTime();
    wait_ms = after_ms - before_ms;
    *(volatile DWORD *)game_address(g_game, pacing->last_frame_duration_ms) = work_ms;
    *(volatile DWORD *)game_address(g_game, pacing->last_wait_ms) = wait_ms;
    *(volatile DWORD *)game_address(g_game, pacing->total_wait_ms) += wait_ms;
    *(volatile DWORD *)game_address(g_game, pacing->previous_frame_time_ms) = after_ms;
}

static BOOL build_stub(void) {
    const PacingLayout *pacing = &g_game->pacing;
    u8 *stub;
    if (g_stub != NULL) return TRUE;
    if (!allocate_executable_stub(32, &stub)) return FALSE;

    /* Preserve both branches of the game's outer limiter gate. */
    stub[0] = 0x80;
    stub[1] = 0x3D;
    encode_u32(&stub[2],
               (u32)(uintptr_t)game_address(g_game, pacing->enforce_limit_flag));
    stub[6] = 0x00;
    stub[7] = 0x74;
    stub[8] = 0x0B;
    stub[9] = 0x56;
    stub[10] = 0xE8;
    encode_rel32(&stub[11], stub + 15, pace_client_frame);
    stub[15] = 0xE9;
    encode_rel32(&stub[16], stub + 20, game_address(g_game, pacing->history_path));
    stub[20] = 0xE9;
    encode_rel32(&stub[21], stub + 25, game_address(g_game, pacing->no_limit_path));
    if (!finalize_executable_stub(stub, 25)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        return FALSE;
    }
    g_stub = stub;
    return TRUE;
}

BOOL frame_pacer_initialize(
    const GameLayout *game, const Config *config, BOOL enabled) {
    if (game == NULL || config == NULL) return FALSE;
    g_game = game;
    g_config = config;
    if (!enabled) return TRUE;
    if (!QueryPerformanceFrequency(&g_qpc_frequency) ||
        g_qpc_frequency.HighPart != 0 || g_qpc_frequency.LowPart == 0) {
        return FALSE;
    }
    return build_stub();
}

u8 *frame_pacer_stub(void) {
    return g_stub;
}

void frame_pacer_reset(void) {
    g_armed = FALSE;
    g_next_deadline.QuadPart = 0;
}
