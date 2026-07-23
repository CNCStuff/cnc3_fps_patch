# Runtime validation

## Compatibility and loader

The signature resolver recognizes the Steam 2012 and EA/Origin-era 2009
Kane's Wrath 1.02 executables, Kane's Wrath 1.03 (2025), and Tiberium Wars
1.09/1.10. Test each build separately; successful signature resolution does
not replace gameplay testing.

Begin with the shipped `target_fps=90` configuration and verify:

- The game reaches the main menu and input works.
- `fps_patch.log` is created beside the DLL.
- The log identifies the expected game, contains `bootstrap_status=1`, the runtime-configuration hook, the
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
- While targeting a player power or unit ability, the placement icon/radius
  must retain its stock opacity pulse. The common XML value
  `OpacityThrobTime="1000"` should remain a one-second cycle.
- Stormrider weapon tracers and Overlord's Wrath must retain their stock sweep,
  emission, lifetime, trail, and impact timing.
- Tiberium Vein Detonation's delayed shatter cue must align with the visible
  explosion, and Shockwave Artillery cues must remain intact.
- Off-screen units must retain animation state when they return to view.
- Pause/unpause, camera movement, zoom, camera shake, Alt-Tab, loading screens,
  saved games, and a second match in the same process must remain stable.

The initial W3D step should be 22, 16, 13, or 11 ms at 45, 60, 75, or
90 FPS respectively. At 60 and 75 FPS the live value alternates as described
in the implementation notes.

## Frame pacing

Capture several minutes with `precise_pacing=1`, then repeat with
`precise_pacing=0`.

The QPC pacer should average close to the configured 45, 60, 75, or 90 Hz.
The integer fallback uses 22, 16, 13, or 11 ms periods and therefore does not
hit every target exactly.

If the spin tail consumes too much CPU, lower `spin_threshold_us`. If the final
deadline jitters, raise it modestly; values above 1000 microseconds need clear
frame-time evidence.

## Alternate rates

After validating the default 90 FPS mode, repeat the checks at 45, 60, and
75 FPS. Verify that one real-time minute advances the same amount of gameplay
and W3D animation at every rate.

## Replay and multiplayer determinism

Do not assume online safety from static analysis alone. Validate:

1. The same deterministic replay at 30, 45, 60, 75, and 90 FPS.
2. Final state and available logic CRC/desync telemetry.
3. Same-rate LAN or online clients.
4. Mixed-rate clients.
5. A deliberately slow peer so network pacing below `1.0` is exercised.

Any CRC mismatch must be investigated with the reviewed patch set intact. Do
not bypass the network gate or overwrite the shared `1/30` value.

## Failure isolation

- No launch: remove the proxy DLL and verify the exact executable version and
  DLL placement.
- Broken input: run with `enabled=0` to separate DirectInput forwarding from
  FPS patch installation.
- Fast visual effects: verify the W3D step sequence and the log message
  `Frame-counted particles, tracers, clouds and Anim2D pinned to retail 30 Hz`.
- Uneven pacing at correct speed: compare precise pacing on and off, then tune
  only `spin_threshold_us`.
- Crash on match start: inspect the session-tail wrapper and live
  `GameEngine`/`GlobalData` pointers.
- Desync: reproduce with a deterministic replay and compare logic CRCs before
  changing any timing patch.
