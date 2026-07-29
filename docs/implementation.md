# Implementation notes

## Loader and installation order

The game imports only `DirectInput8Create` from `DINPUT8.dll`, making a local
`dinput8.dll` proxy a small loader. The proxy also exports the standard COM DLL
entry points and forwards every export to the real System32 DLL.

The executable's CRT initializers must run while `g_clientUpdateFPS` is still
30. Several static initializers derive secondary values from the retail rate.
For that reason, the DLL's normal CRT startup calls a deliberately small
`DllMain` which does not change timing state, parse files, allocate detour
trampolines, or start a worker thread.

During process attach it performs only these guarded operations:

1. Validate that the loaded image is a sane 32-bit x86 PE.
2. Scan executable sections for the required masked signatures and require one
   unique match for every semantic patch site.
3. Decode relative call targets and absolute global operands from the matched
   instructions, then validate their relationships and the 15/30 Hz retail
   invariants.
4. Redirect a late runtime-configuration call to the ABI-matched DLL wrapper.
5. Redirect the tail jump in `GameEngine_StartGameSession` to another wrapper.

Both wrappers call the original target first. The actual configuration,
logging, runtime patch allocation, and timing writes therefore execute later,
outside the loader lock and after CRT initialization.

## Bootstrap redirections

The following RVAs are examples from the Steam 2012 Kane's Wrath executable.
The DLL no longer uses them as build identity: it resolves the equivalent
sites from masked instruction signatures at startup.

```text
RVA 0x0025368E
  original: E8 87 BA 12 00
  role: final call in GameEngine_ApplyRuntimeConfiguration
  patch: CALL runtime_config_thiscall_hook

RVA 0x00255F95
  original: E9 86 2E FF FF
  role: tail jump from GameEngine_StartGameSession
  patch: JMP start_session_tail_hook
```

This avoids relocating arbitrary function prologues. Kane's Wrath wraps the
final `__thiscall` in its runtime-configuration method. Tiberium Wars has no
equivalent final call, so its signature selects the nearby no-argument
subsystem-checksum call; that wrapper preserves the original EAX result before
the caller stores it. Both wrappers call the original target before applying
the FPS configuration. The session wrapper calls the original tail target,
reapplies live fields, resets the 64-frame history, and re-arms pacing.

The two variants deliberately use separate signatures and wrapper prototypes.
Matching Tiberium Wars must never reinterpret its no-argument call as the
Kane's Wrath `__thiscall` site. Resolution also rejects an image if both
bootstrap variants match, because silently choosing the wrong ABI would be
unsafe.

## Static visual patches

The shared retail float at RVA `0x0061D04C` remains unchanged because one user
is authoritative pathfinding code. Only these instruction operands are
redirected to the process-lifetime DLL float:

```text
RVA 0x00097ED0  camera visual step operand
RVA 0x000A6C5E  laser visual step operand
RVA 0x000BE145  scripted-model transition step operand
```

Each instruction operand is checked against the relocated retail address at
the point it is replaced. All three writes share one transaction, so a later
mismatch or write failure restores the earlier operands.

## Camera input normalization

Kane's Wrath and Tiberium Wars evaluate tactical camera input once per client
frame. Raising the client rate therefore repeats several stock per-frame
operations too often unless they are normalized independently. TW 1.09/1.10
and all supported KW builds share the relevant instruction sequences and
object layouts byte-for-byte, despite their functions residing at different
addresses.

Representative VAs are shown below. The resolver does not use these addresses
as build identity:

```text
Build                 scrollBy   rotation   zoom in/out          adjust     shake operand
KW 1.02 Steam 2012    0x49B160   0x95F7F2   0x95F81A/0x95F832   0x49AEBF   0x49AC2F
TW 1.09               0x4987F0   0x99B8AB   0x99B8D3/0x99B8EB   0x49854C   0x498326
TW 1.10               0x82F635   0x778AD7   0x778AFF/0x778B17   0x82F391   0x82F16B
```

Resolution also proves that both held-zoom behaviors use the same W3DView
singleton, keyboard rotation uses the same GlobalData singleton as the timing
patch, W3DView slot `0x14` still points to the matched `scrollBy` function, and
camera shake still references the retail `0.75` constant.

### Scrolling and arrow keys

Edge scrolling, right-mouse drag scrolling, and arrow-key movement all
converge on the same `W3DView::scrollBy` virtual method. The patch replaces the
live derived-class vtable entry after proving that it still points to the
signature-resolved function. Its wrapper scales the two-dimensional delta by:

```text
30 / targetFPS
```

Hooking the convergence point keeps both world displacement and the view's
stored scroll amount in the same units. The latter is subsequently consumed by
terrain-height adjustment, so scaling only an earlier input branch would leave
the camera's internal state inconsistent.

### Held zoom

The two held-key behaviors apply affine transforms every client frame:

```text
zoom in:  z' = 0.96 * z - 1
zoom out: z' = 1.05 * z + 1
```

A linear `30 / targetFPS` multiplier is not exact for a recurrence with both a
multiplier and an offset. For each supported rate, the replacement behavior
uses the fractional affine step whose repeated one-second transform equals 30
applications of the retail step:

```text
a_target = a_retail ** (30 / targetFPS)
b_target = b_retail * (1 - a_target) / (1 - a_retail)
```

Only the held-key behavior entries are redirected. Mouse-wheel zoom remains in
the stock event handler and therefore still performs one original zoom step per
physical wheel detent rather than acquiring an inappropriate frame-rate scale.

### Rotation, settling, and shake

Keyboard rotation reads `GlobalData::KeyboardCameraRotateSpeed` as a per-frame
amount, so the live setting is scaled by `30 / targetFPS` at each session
boundary.

Terrain-height settling uses `GlobalData::CameraAdjustSpeed` as the blend
coefficient in an exponential recurrence. Its retail-equivalent coefficient is:

```text
adjust_target = 1 - (1 - adjust_retail) ** (30 / targetFPS)
```

The DLL uses the normal Universal CRT `powf` implementation for this fractional
exponent. Authored settings outside the ordinary `(0, 1)` blend range are
preserved unchanged.

Camera shake multiplies its amplitude by `0.75` once per retail client frame.
The operand is redirected to a DLL-owned `0.75 ** (30 / targetFPS)` constant,
preserving the same real-time decay at 45, 60, 75, and 90 FPS.

GlobalData can be restored by a new game session. The patch remembers both the
authored values and the values it last applied, so session reloads are
renormalized without accidentally treating an already-normalized value as the
new retail baseline.

## Keyboard autorepeat

The stock keyboard implementation measures held-key autorepeat with a private
frame counter. Raising the client rate without separating that counter shortens
the initial delay and makes Backspace, Delete, and cursor movement repeat at the
configured client FPS instead of their retail rate.

### Stock control flow

The relevant implementation is instruction-for-instruction identical in all
five supported images; only relative call destinations move. These are virtual
addresses for the preferred `0x00400000` image base:

| Build | update entry | frame increment | hardware-poll call | repeat call | repeat entry |
|---|---:|---:|---:|---:|---:|
| KW 1.02 Steam 2012 | `0x0056F588` | `0x0056F58B` | `0x0056F591` | `0x0056F3FC` | `0x0056F28A` |
| KW 1.02 EA/Origin 2009 | `0x0049BB4D` | `0x0049BB50` | `0x0049BB56` | `0x0049B9C1` | `0x0049B84F` |
| KW 1.03 | `0x004DC444` | `0x004DC447` | `0x004DC44D` | `0x004DC2B8` | `0x004DC0C2` |
| TW 1.09 | `0x00568840` | `0x00568843` | `0x00568849` | `0x005686B4` | `0x005684BE` |
| TW 1.10 | `0x00495F78` | `0x00495F7B` | `0x00495F81` | `0x00495DEC` | `0x00495C7A` |

The 22-byte update method has this complete shape:

```asm
push esi
mov  esi, ecx
inc  dword ptr [esi+0E28h] ; Keyboard::inputFrame
call poll_hardware
mov  ecx, esi
pop  esi
jmp  process_events
```

`process_events` copies each physical event's state into the 256-entry key
table and timestamps that key with the same `inputFrame`:

```asm
mov ecx, [esi+0E28h]
mov [esi+eax*8+2Ch], ecx   ; key[eax].sequence = inputFrame
...
mov ecx, esi
call check_key_repeat
```

The stock 111-byte repeat routine scans the table in DirectInput key-code
order. A held key becomes eligible when:

```asm
mov edx, [esi+0E28h]
sub edx, [ecx]
cmp edx, 0Ah
ja  generate_repeat
```

It queues one event whose state is `0x0102` (`KEY_DOWN | AUTOREPEAT`), resets
every key timestamp to the current frame, and then backdates the selected key:

```asm
mov word ptr [event.state], 102h
...
mov eax, [inputFrame]
sub eax, 0Ch
mov [esi+ebx*8+2Ch], eax
```

The unsigned `> 10` test produces the first repeat after eleven keyboard
frames. The `inputFrame - 12` backdate makes the selected key eligible on the
next invocation, so sustained repeat occurs once per keyboard update. At stock
30 FPS that is a 366.7 ms initial delay and 30 repeats/s; an unpatched 90 FPS
client reduces the delay to 122.2 ms and emits 90 repeats/s.

### Resolver and patch

There are no per-build keyboard signatures. One shared 22-byte update
signature and one shared 111-byte repeat-body signature each match exactly
once in TW 1.09, TW 1.10, KW 1.02 Steam, KW 1.02 EA/Origin, and KW 1.03. The
only wildcard bytes are relocated `rel32` operands.

The resolver decodes the update method's hardware-poll call and tail jump. From
the decoded event processor it verifies the physical-event timestamp store at
offset `+0x26`, the repeat-call block at `+0x79`, and that the call at `+0x7F`
targets the signature-validated repeat body. Resolution fails before any write
if any relationship differs.

Installation makes three transactional changes:

```text
INC [Keyboard+0x0E28]  -> six NOPs
CALL hardware_poll     -> CALL poll_keyboard_at_retail_rate
CALL check_key_repeat  -> CALL check_keyboard_repeat_at_retail_rate
```

The poll wrapper uses the already-imported millisecond clock and a small
fixed-point accumulator:

```c
scaled = remainder + elapsed_ms * 30;
repeat_tick = scaled >= 1000;
remainder = scaled % 1000;
```

At most one logical tick is emitted per physical poll, so a stall cannot cause
a burst of queued repeats. The wrapper advances `Keyboard::inputFrame` only on
that 30 Hz tick but calls the original hardware poll unconditionally. Physical
input latency therefore remains at the configured client rate.

An event detected between logical ticks is timestamped for the next logical
tick. This models the frame on which a retail 30 FPS client would first have
observed it and preserves the full eleven-tick initial delay. The original
repeat routine remains intact, including its key ordering, one-event limit,
queue insertion, `0x0102` flag, and timestamp-reset behavior.

Using elapsed time rather than counting calls is important because not every
caller runs at the configured client rate. It also prevents an immediate extra
poll from advancing the repeat clock twice. The normal client update, APT
loading-screen updates, the approximately 20 ms loading wait loop, and the
conditional extra-poll path all enter the same patched virtual method; there is
no second repeat implementation.

### Downstream keyboard audit

The affected native text-entry path was checked in both KW 1.02 Steam and TW
1.10. Their wrappers (`0x004AEB30` and `0x00842F2B`) call the common handlers
at `0x005030A7` and `0x0042C887`. Those handlers require the key-down bit but do
not reject `AUTOREPEAT` for Backspace (DirectInput code 14), Delete, Left,
Right, Home, End, or the modified Left/Right word-navigation branches. All of
those editing operations therefore receive the corrected retail cadence.

Ordinary mapped gameplay commands do not inherit the bug. The keyboard event
emitter creates game-message IDs 23 and 24 and includes the full state word.
The matched-key path in `MetaEventTranslator` tests bit `0x0100` before emitting
a mapped command (`0x004E3E17` in KW 1.02 Steam and `0x0040E2EF` in TW 1.10).
Synthetic repeats are consequently suppressed for normal MetaMap hotkeys while
native GUI gadgets continue to receive them.

The tactical-camera held-key behaviors are a separate once-per-client-frame
path. They are already covered by the scroll, rotation, and held-zoom
normalization described above. In KW 1.02 Steam the relevant behavior entries
are `0x0095F780`, `0x0095F7BA`, `0x0095F7F2`, `0x0095F81A`, and `0x0095F832`.

A targeted scan of the five binaries finds the same eight `Keyboard+0x0E28`
instructions in each keyboard implementation: initialization/reset, the two
repeat reads, physical-event timestamping, two queue/reset reads, and the update
increment. Each image has one additional instruction elsewhere that happens to
use displacement `0x0E28` with a different object; it is not a Keyboard
reference. No other timer exists inside `Keyboard`.

Other direct key/modifier reads are instantaneous event-time decisions, such
as focus navigation, selection modifiers, rally-point placement, or build
button variants. The tracked-key release scan emits an end message only when a
key changes from down to up; calling it more often reduces release latency but
does not accumulate actions.

## Six-phase logic scheduler

The authoritative 15 Hz logic tick is split into six ordered phases. The
stock batching formula works when `clientFPS / 15` divides six, which covers
45 and 90 FPS but makes unmodified 60 and 75 FPS run slowly.

The following examples are from Kane's Wrath 1.02 Steam 2012:

```text
RVA 0x002560A6  GameEngine_DispatchLogicPhase batching arithmetic
RVA 0x0024333E  GameEngine_UpdatePhaseInterpolation
RVA 0x001EF10F  GameLogic_UpdatePhase client-slice Drawable flush
```

For ratio `R = targetFPS / 15` and the first pending phase `P`, the replacement
dispatcher calculates:

```c
client_slot = (R * P + 5) / 6;
end_phase = (6 * client_slot) / R;
```

This produces:

```text
45 FPS: 1-2, 3-4, 5-6
60 FPS: 1, 2-3, 4, 5-6
75 FPS: 1, 2, 3, 4, 5-6
90 FPS: 1, 2, 3, 4, 5, 6
```

Interpolation uses `client_slot / R` at 60 and 75 FPS, giving evenly spaced
quarter- or fifth-tick samples. The 45/90 paths retain their stock
`phase / 6` calculation.

The third hook preserves the stock call which drains pending Object updates
into Drawable state, but invokes it only after the final phase assigned to the
current client frame. The stock modulo condition would invoke it after every
phase at ratios four and five, including twice inside the final batched frame.

`GameEngine_IsClientFrameBoundary` does not require a patch. With the schedules
above, its existing `phase == 6 / R` test still identifies the first client
slice of each logic tick.

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
frames make the existing manager naturally skip extra high-rate updates while
preserving its original emission and lifetime arithmetic.

The general FX particle engine is also frame-integrated. CPU particle life is
decremented once per simulation call, and its physics/color/alpha modules add
per-frame deltas without a `dt` argument. System lifetime and trail/swarm
storage follow the same convention. The DLL redirects this direct call:

```text
RVA 0x0009F9AC
  original: CALL FXParticleSystem_ParticleSystemManager_UpdateForClientFrame
  patch:    CALL update_particles_at_retail_rate
```

The wrapper uses a 30/target fixed-point accumulator, producing exactly 30
particle-simulation calls per nominal second. It deliberately hooks the base
simulation call inside `W3DParticleSystemManager`, not the outer manager call.
W3D render-buffer preparation therefore still runs on every client
frame.

GPU particles have a second timebase consistency issue. Particle creation
uses the retail startup scalar `g_gpuParticleFramesPerMillisecond = 0.03`, but
expiration directly multiplies W3D milliseconds by the live
`g_clientUpdateFPS`. The operand at RVA `0x003855A9` is redirected to a
DLL-owned constant 30, so creation and expiration use the same visual frame.

These fixes intentionally retain 30 Hz state advancement for legacy
frame-counted tracers and particles. Their W3D rendering still occurs at the
configured rate, but CPU particle positions may repeat between simulation updates;
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

### Radius-cursor opacity throb

Ability-placement overlays are `RadiusCursorLibrary` decals. Their XML defines
an opacity range and a millisecond period, normally:

```xml
OpacityMin="30"
OpacityMax="60"
OpacityThrobTime="1000"
```

`RadiusDecalInstance_UpdateForClientFrame` reads the raw client-frame number
and converts `OpacityThrobTime` to a frame count by multiplying it by the
shared retail constant `0.03 frames/ms`. At 90 FPS that makes a 1000 ms period
only 30 frames, so it completes in roughly 333 ms.

The multiplication operand at Steam 2012 RVA `0x00150F7B` is redirected to a
DLL-owned `targetFPS / 1000` float. Thus a 1000 ms period becomes exactly one
second at every supported rate. The original shared `0.03` scalar is not changed because
GPU particle creation intentionally uses a fixed 30-frame visual time domain.

The remainder of the same radius-decal update already uses
`g_visualSecondsPerClientFrame` for rotation, so it is covered by the existing
live timing-cache update.

The display limiter branch changes as follows:

```text
RVA 0x000A5B61
  7D 13  JGE display_wait_finished
  EB 13  JMP display_wait_finished
```

The destination that updates `g_previousPresentTimeMs` remains in the path.

## Live state initialization

The supported targets and client-to-logic ratios are:

```text
45 FPS -> ratio 3
60 FPS -> ratio 4
75 FPS -> ratio 5
90 FPS -> ratio 6
```

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
the three caches respectively. Audio also needs a rate-converted subsystem
update because request admission and `Limit` lifetime are client-frame
operations rather than millisecond integration alone.

### Audio request and `Limit` cadence

`CashGaining` and `CashLosing` are zero-delay `Limit=1` one-shots. The Money
logic submits them through the normal audio-event virtual method. The manager
checks the active and pending request lists before admitting each event, then
moves requests through preparation, physical playback, completion, and list
cleanup from `SageAudioManager_UpdateForClientFrame`.

Raising the complete client rate also calls that manager 45--90 times per
second and interleaves it between logic-phase slices that retail processes as
one group. Pending requests can become playing requests, completed instances
can leave the lists, and a `Limit` slot can become available between phases
where retail performs no audio service. This is why money gain/spend ticks can
be admitted much more frequently even though authoritative Money logic remains
15 Hz. This manager-level cadence is the remaining rate-sensitive mechanism
consistent with the recorded symptom; the replacement below still requires a
fresh vanilla-versus-patched runtime comparison.

An earlier implementation redirected three frame-duration reads inside
`SageAudioManager_PrepareAndQueueRequest`. Runtime comparison showed that it
did not correct the money sounds. Static control flow explains why: after the
eligibility/`Limit` pre-check, a successful ordinary request jumps directly to
queue insertion and skips all three reads. That block handles a request whose
pre-check failed and is being retained for deferred/retry processing; it is not
the normal accepted initial-request path. The operand redirections were
therefore removed.

The current patch redirects the manager's vtable slot 5 through a small
Bresenham-style rate converter:

```c
audioAccumulator += 30;
if (audioAccumulator >= targetFPS) {
    audioAccumulator -= targetFPS;
    originalAudioUpdate(manager);
}
```

The accumulator is seeded so audio service runs before the first logic-phase
slice. At 60 and 90 FPS this invokes the original manager every two or three
client frames. The 45 and 75 FPS schedules distribute two audio updates across
three or five client slices without accumulating long-term drift. The original
manager, request logic, event limits, playback lists, and physical-renderer
callbacks remain unchanged.

`SageAudioManager_ComputeClientFrameDeltaMilliseconds` reads the difference
between the current and previous client-frame numbers. After skipped wrapper
calls it therefore sees the complete span and multiplies it by
`1000/targetFPS`; for example, three 90 FPS client-frame intervals produce the
same `33.333 ms` delta as one retail audio update. Authored delays,
multisounds, and loop intervals retain their millisecond progression while
queue admission and cleanup are intended to return to the retail 30 Hz service
domain.

The resolver finds one unique stable tail inside the update function, verifies
its entry 0x12A bytes earlier, and requires exactly one read-only absolute xref
to that entry. That xref is the vtable slot patched transactionally at runtime:

```text
Build                    update function RVA  vtable-slot RVA
KW 1.02 Steam 2012       0x00068484           0x00619DD8
KW 1.02 EA 2009          0x00368D22           0x00698D20
KW 1.03                  0x003B5C49           0x006A4C98
TW 1.09                  0x00068A5D           0x006354C8
TW 1.10                  0x00400C74           0x006C6590
```

The affected request state is audio-side, and audio filename/delay
randomization advances a dedicated RNG rather than `g_gameLogicRandomState`.
`GameLogic_ComputeCRC` hashes the logic RNG state and has no direct reference
to the audio RNG or these request-delay values. This makes ordinary audio a
low-probability multiplayer-desync source, not a statically proven impossible
one. Completed sounds can update ScriptEngine state, and Worldbuilder exposes
sound- and speech-completion conditions that can execute scripts. Scripted or
custom multiplayer content therefore remains a possible indirect
audio-to-gameplay bridge and needs synchronized runtime testing.

This is not a blanket rewrite of startup timing state. The nearby 15 Hz logic
FPS and seconds-per-logic-frame globals remain unchanged, as do the many
startup `g_clientUpdateFPS / 2` values. The GPU particle creation scalar also
remains `0.03`: its shader code explicitly uses a 30-frames-per-second particle
time domain, and only the inconsistent expiry operand is redirected.

The authoritative `g_logicFPS` is validated as 15 and never written.

### Retail-equivalent W3D time

Retail advances its central W3D clock by an integer 33 ms on each of 30 client
frames, or 990 W3D milliseconds per nominal second. Constant integer steps
preserve that rate at 45 and 90 FPS, but not at 60 or 75 FPS.

Two sequences in `W3DDisplay_RenderAndPresentFrame` are redirected to one
remainder-based helper:

```text
RVA 0x000A5A7C  reduced-render/special-path advance
RVA 0x000A5AB7  normal client-frame-delta advance
```

The helper accumulates `clientFrameDelta * 990`, divides by `targetFPS`, and
retains the integer remainder. Its per-frame steps are:

```text
45 FPS: 22
60 FPS: 16, 17
75 FPS: 13, 13, 13, 13, 14
90 FPS: 11
```

It also publishes the next step through the game's live W3D
milliseconds-per-client-frame field. Camera and other W3DView functions which
read that field therefore follow the same long-term 990 ms rate instead of
remaining at a truncated constant 16 or 13 ms.

## Retail-equivalent stock pacing

The separate 29 ms presentation wait is bypassed, but the outer limiter remains
the game's original `timeGetTime`/`Sleep(0)` implementation.
Its limit/no-limit branches, timestamp updates, accumulated wait telemetry,
64-frame performance history, and adaptive update multiplier are not replaced.

Only its 25-byte interval calculation is redirected. In the three supported KW
layouts that block begins at:

```text
Build                    interval block RVA
KW 1.02 Steam 2012       0x00256457
KW 1.02 EA/Origin 2009   0x00184198
KW 1.03                  0x001C4012
```

The original block converts
`millisecondsPerLogicFrame / (updateMultiplier * networkScale)` directly to
one integer client-frame interval. At 60 and 75 FPS this truncates every frame
independently to 16 or 13 ms, running complete logic cycles faster than
retail. An exact-nominal QPC replacement instead produces a 66.667 ms cycle,
which is about one percent slower than the observed retail limiter and removes
its natural millisecond scheduling variation.

The replacement derives and truncates one retail client-frame interval first:

```text
retailFrameMs = trunc((1000 / 15) / (2 * networkScale))
logicCycleBudgetMs = 2 * retailFrameMs
```

It then uses a quotient/remainder accumulator to distribute that integer
budget across `GameEngine::pacing_update_multiplier` high-rate frames. With
normal network scale and the configured multiplier, the schedules are:

```text
45 FPS: 22, 22, 22                 = 66 ms per logic cycle
60 FPS: 16, 17, 16, 17             = 66 ms per logic cycle
75 FPS: 13, 13, 13, 13, 14         = 66 ms per logic cycle
90 FPS: 11, 11, 11, 11, 11, 11     = 66 ms per logic cycle
```

Network pacing is retained in the retail domain. For example, scale `0.95`
produces the same 35 ms retail frame and 70 ms retail logic-cycle budget as the
stock calculation, then subdivides that budget across the selected client
rate. If the engine lowers its adaptive update multiplier, the same remainder
logic proportionally lengthens the cycle. A divisor change rescales the saved
fractional phase instead of discarding it.

This removes the QPC deadline, generated executable stub, `Sleep(1)`/spin
tail, and their configuration. More importantly for audio cadence, it retains
the stock wait loop's millisecond granularity, scheduler jitter, skipped-limit
behavior, and oversleep handling. The helper computes only a local wait
interval; no clock value is stored in simulation state or included in the game
CRC.

## Patch safety

- Every required signature must match exactly once.
- Derived call targets, absolute operands, related globals, and retail timing
  invariants are validated while resolving the executable layout.
- Each runtime patch site is checked again immediately before its write.
- ASLR-adjusted absolute operands are validated against the loaded module base.
- Code writes use `VirtualProtect` and `FlushInstructionCache`.
- Bootstrap and static visual writes use patch transactions that capture the
  original bytes and roll back in reverse order if a later write fails.
- A failure disables further patch work while DirectInput forwarding remains
  available.
- The proxy is process-lifetime state and is not designed for hot unloading.
