# Runtime validation

## Compatibility and loader

The signature resolver recognizes the Steam 2012 and EA/Origin-era 2009
Kane's Wrath 1.02 executables and Kane's Wrath 1.03 (2025). Test each build
separately; successful signature resolution does not replace gameplay testing.

Begin with `target_fps=45` and verify:

- The game reaches the main menu and input works.
- `kw_fps_patch.log` is created beside the DLL.
- The log contains `bootstrap_status=1`, the runtime-configuration hook, the
  session hook, and the static-patch installation messages.
- The process exits normally.

A rejected signature or patch site indicates an unsupported executable or a
damaged patch installation. Keep the guards enabled.

## Gameplay timing

Compare the same short skirmish against an unpatched 30 FPS run:

- Unit movement, construction, attack cadence, and resource income must retain
  stock speed.
- Idle, reverse, ping-pong, and blended unit animations must retain stock
  duration while displaying additional rendered poses.
- Muzzle flashes, lasers, particles, decals, radar fades, edge scrolling,
  sprite animation, and weather lightning must retain stock timing.
- Stormrider weapon tracers and Overlord's Wrath must retain their stock sweep,
  emission, lifetime, trail, and impact timing.
- Tiberium Vein Detonation's delayed shatter cue must align with the visible
  explosion, and Shockwave Artillery cues must remain intact.
- Off-screen units must retain animation state when they return to view.
- Pause/unpause, camera movement, zoom, camera shake, Alt-Tab, loading screens,
  saved games, and a second match in the same process must remain stable.

The log should report a W3D step of 22 ms at 45 FPS and 11 ms at 90 FPS.

## Frame pacing

Capture several minutes with `precise_pacing=1`, then repeat with
`precise_pacing=0`.

The QPC pacer should average close to 45.000 or 90.000 Hz. The integer fallback
tends toward approximately 45.45 or 90.91 Hz before workload and scheduler
effects.

If the spin tail consumes too much CPU, lower `spin_threshold_us`. If the final
deadline jitters, raise it modestly; values above 1000 microseconds need clear
frame-time evidence.

## 90 FPS load

After 45 FPS is stable, set `target_fps=90`, restart the process, and repeat the
gameplay checks. Include a large battle to expose CPU-bound slowdown. Visual
state should advance at stock real-time speed while rendering more frequently.

## Replay and multiplayer determinism

Do not assume online safety from static analysis alone. Validate:

1. The same deterministic replay at 30, 45, and 90 FPS.
2. Final state and available logic CRC/desync telemetry.
3. Same-rate LAN or online clients.
4. Mixed 30/45 and 30/90 clients.
5. A deliberately slow peer so network pacing below `1.0` is exercised.

Any CRC mismatch must be investigated with the reviewed patch set intact. Do
not bypass the network gate, change the six-phase scheduler, or overwrite the
shared `1/30` value.

## Failure isolation

- No launch: remove the proxy DLL and verify the exact executable version and
  DLL placement.
- Broken input: run with `enabled=0` to separate DirectInput forwarding from
  FPS patch installation.
- Fast visual effects: verify the 22/11 ms W3D step and the log message
  `Frame-counted particles, tracers, clouds and Anim2D pinned to retail 30 Hz`.
- Uneven pacing at correct speed: compare precise pacing on and off, then tune
  only `spin_threshold_us`.
- Crash on match start: inspect the session-tail wrapper and live
  `GameEngine`/`GlobalData` pointers.
- Desync: reproduce with a deterministic replay and compare logic CRCs before
  changing any timing patch.
