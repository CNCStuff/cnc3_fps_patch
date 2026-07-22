# Implementation notes

## Loader and installation order

The game imports only `DirectInput8Create` from `DINPUT8.dll`, making a local
`dinput8.dll` proxy a small loader. The proxy also exports the standard COM DLL
entry points and forwards every export to the real System32 DLL.

The executable's CRT initializers must run while `g_clientUpdateFPS` is still
30. Several static initializers derive secondary values from the retail rate.
For that reason, `DllMainCRTStartup` does not change timing state, parse files,
allocate detour trampolines, or start a worker thread.

During process attach it performs only these guarded operations:

1. Validate that the loaded image is a sane 32-bit x86 PE.
2. Scan executable sections for the required masked signatures and require one
   unique match for every semantic patch site.
3. Decode relative call targets and absolute global operands from the matched
   instructions, then validate their relationships and the 15/30 Hz retail
   invariants.
4. Redirect one existing tail call in
   `GameEngine_ApplyRuntimeConfiguration` to a DLL wrapper.
5. Redirect the tail jump in `GameEngine_StartGameSession` to another wrapper.

Both wrappers call the original target first. The actual configuration,
logging, runtime patch allocation, and timing writes therefore execute later,
outside the loader lock and after CRT initialization.

## Bootstrap redirections

The following RVAs are examples from the Steam 2012 executable. The DLL no
longer uses them as build identity: it resolves the equivalent sites from
masked instruction signatures at startup.

```text
RVA 0x0025368E
  original: E8 87 BA 12 00
  role: final call in GameEngine_ApplyRuntimeConfiguration
  patch: CALL kw_runtime_config_tail_hook

RVA 0x00255F95
  original: E9 86 2E FF FF
  role: tail jump from GameEngine_StartGameSession
  patch: JMP kw_start_session_tail_hook
```

This avoids relocating arbitrary function prologues. The first wrapper keeps
the original `__thiscall` ECX value, calls the original tail target, and then
applies the FPS configuration. The session wrapper calls the original tail
target, reapplies live fields, resets the 64-frame history, and re-arms pacing.

## Static visual patches

The shared retail float at RVA `0x0061D04C` remains unchanged because one user
is authoritative pathfinding code. Only these instruction operands are
redirected to the process-lifetime DLL float:

```text
RVA 0x00097ED0  camera visual step operand
RVA 0x000A6C5E  laser visual step operand
RVA 0x000BE145  scripted-model transition step operand
```

The instruction opcodes and relocated retail operand are validated before any
of the three writes occur.

## Frame-counted FX compatibility

The central W3D clock correction does not cover systems whose asset data is
defined directly in client frames. The tracer and particle engines retain an
explicit retail 30 Hz convention.

`TracerModelDraw` data contains `MinTracersPerFrame`, `MaxTracersPerFrame`,
`SweepSpeed`, and `FrameLifeTime`. `W3DTracerManager` consumes those values
using the absolute client-frame number. Stormrider weapon streaks therefore
emit, sweep, move, and expire three times too quickly if the raw 90 Hz frame
number is used.

Both tracer frame reads are redirected to a DLL helper:

```text
RVA 0x0009D5B3  tracer-manager reset frame read
RVA 0x0009D5D0  tracer-manager update frame read
```

The helper returns `ceil(clientFrame * 30 / targetFPS)`. Repeated synthetic
frames make the existing manager naturally skip extra 45/90 Hz updates while
preserving its original emission and lifetime arithmetic.

The general FX particle engine is also frame-integrated. CPU particle life is
decremented once per simulation call, and its physics/color/alpha modules add
per-frame deltas without a `dt` argument. System lifetime and trail/swarm
storage follow the same convention. The DLL redirects this direct call:

```text
RVA 0x0009F9AC
  original: CALL FXParticleSystem_ParticleSystemManager_UpdateForClientFrame
  patch:    CALL kw_fx_particle_simulation_update_at_retail_rate
```

The wrapper uses a 30/target fixed-point accumulator, producing exactly 30
particle-simulation calls per nominal second. It deliberately hooks the base
simulation call inside `W3DParticleSystemManager`, not the outer manager call.
W3D render-buffer preparation therefore still runs on every 45/90 Hz client
frame.

GPU particles have a second timebase consistency issue. Particle creation
uses the retail startup scalar `g_gpuParticleFramesPerMillisecond = 0.03`, but
expiration directly multiplies W3D milliseconds by the live
`g_clientUpdateFPS`. The operand at RVA `0x003855A9` is redirected to a
DLL-owned constant 30, so creation and expiration use the same visual frame.

These fixes intentionally retain 30 Hz state advancement for legacy
frame-counted tracers and particles. Their W3D rendering can still occur at
45/90 Hz, but CPU particle positions may repeat between simulation updates;
making every legacy particle module smoothly variable-step would require a
larger per-module rewrite.

Two more absolute-frame consumers use the same synthetic frame helper:

```text
RVA 0x00082D00  W3DCloudEffectManager current-frame read
RVA 0x00374FAD  Anim2D frame-change timestamp read
RVA 0x00379682  Anim2D current-frame update read
```

Cloud/lightning checks an existing last-frame field before running its chance,
frequency, and frame-duration arithmetic. Supplying the synthetic 30 Hz frame
therefore lets its stock duplicate-frame guard throttle the state naturally.

Anim2D stores the current frame when it selects a sprite frame and later uses
`current - stored >= frameInterval`. Both reads are patched as a pair. Patching
only the update read would mix raw and synthetic clocks and make the unsigned
subtraction invalid.

The display limiter branch changes as follows:

```text
RVA 0x000A5B61
  7D 13  JGE display_wait_finished
  EB 13  JMP display_wait_finished
```

The destination that updates `g_previousPresentTimeMs` remains in the path.

## Live state initialization

For 45 Hz, the ratio is 3 and the W3D step is 22 ms. For 90 Hz, the ratio is 6
and the W3D step is 11 ms.

The wrapper updates:

```text
g_clientUpdateFPS
GlobalData::UseFPSLimit
GlobalData::FramesPerSecondLimit
GameEngine::max_update_fps
GameEngine::nominal_client_frames_per_logic_tick
GameEngine::pacing_update_multiplier
GameEngine::frame_duration_history_ms[64]
GameEngine::frame_duration_history_sum_ms
GameEngine::frame_duration_history_index
W3D milliseconds per client frame
g_clientFramesPerSecondFloat
g_audioMillisecondsPerClientFrame
g_visualSecondsPerClientFrame
```

In the Steam 2012 build these three values are at VAs `0x00C0D4F4`,
`0x00C0D4F8`, and `0x00C0D5BC`. The resolver derives their locations from CRT
initializer signatures. The first and third cover stream integration,
dynamic decals, scripted-model transition lengths, Drawable fades,
radar/client visuals, and edge-scroll acceleration.

The middle cache's initializer at `0x00A04A3E` divides the literal `1000.0f`
by `g_clientUpdateFPS`, proving that its unit is milliseconds per client frame:

```text
g_audioMillisecondsPerClientFrame = 1000.0f / g_clientUpdateFPS
```

`SageAudioManager_ComputeClientFrameDeltaMilliseconds` multiplies the raw
client-frame difference by this value. At 90 Hz, retaining the retail
`33.333` ms advances XML-authored audio delays three times too quickly; the
correct value is approximately `11.111` ms. A seconds-per-frame value such as
`1/targetFPS` would be 1000 times too small and would break delayed and
multisound cues.

The DLL therefore writes `targetFPS`, `1000/targetFPS`, and `1/targetFPS` to
the three caches respectively. No audio instruction detour is required.

This is not a blanket rewrite of startup timing state. The nearby 15 Hz logic
FPS and seconds-per-logic-frame globals remain unchanged, as do the many
startup `g_clientUpdateFPS / 2` values. The GPU particle creation scalar also
remains `0.03`: its shader code explicitly uses a 30-frames-per-second particle
time domain, and only the inconsistent expiry operand is redirected.

The authoritative `g_logicFPS` is validated as 15 and never written.

## QPC pacing hook

When `precise_pacing=1`, the nine-byte stock limiter gate at RVA `0x00256449`
is replaced with a jump to a generated 25-byte x86 stub. The original bytes,
including the ASLR-relocated absolute operand, are validated first.

The stub preserves both original control paths:

```asm
cmp byte ptr [g_enforceFPSLimitThisFrame], 0
je  no_limit

push esi                    ; GameEngine *
call kw_pace_client_frame   ; __stdcall
jmp pacing_history_update

no_limit:
jmp original_no_limit_path
```

The C pacer uses an absolute `QueryPerformanceCounter` deadline. Its period is
still calculated from the game's live milliseconds-per-logic-frame,
`GameEngine::pacing_update_multiplier`, and network pacing scale. Consequently
the normal adaptive and synchronized multiplayer slowdown remain represented.

The deadline is re-anchored after a long pause or skipped-limiter interval. A
short configurable spin tail avoids throwing away sub-millisecond precision;
the coarse portion of the wait uses `Sleep(1)`, `SwitchToThread`, or `Sleep(0)`.

Setting `precise_pacing=0` leaves the stock outer limiter in place. Its 22/11
ms integer periods are slightly above the requested rate, but it provides a
direct fallback when isolating QPC pacing behavior.

## Patch safety

- Every required signature must match exactly once.
- Derived call targets, absolute operands, related globals, and retail timing
  invariants are validated before the first static visual write.
- ASLR-adjusted absolute operands are validated against the loaded module base.
- Code writes use `VirtualProtect` and `FlushInstructionCache`.
- Static visual writes have best-effort rollback if a later write fails.
- A failure disables further patch work while DirectInput forwarding remains
  available.
- The proxy is process-lifetime state and is not designed for hot unloading.
