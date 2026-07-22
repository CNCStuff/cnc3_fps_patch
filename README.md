# Kane's Wrath fixed-rate FPS patch

[![Build DLL](https://github.com/CNCStuff/cnc3_fps_patch/actions/workflows/build.yml/badge.svg?branch=main&event=push)](https://github.com/CNCStuff/cnc3_fps_patch/actions/workflows/build.yml?query=branch%3Amain+event%3Apush)

A 32-bit Windows `dinput8.dll` proxy that runs the Kane's Wrath client and
render loop at **45 or 90 FPS** while retaining the stock **15 Hz authoritative
logic rate**.

The implementation and documentation are LLM-written by **GPT-5.6 Sol**, based
on reverse engineering of Kane's Wrath game binaries.

## Compatibility

- Platform: 32-bit Windows game process; the DLL can be cross-compiled on
  macOS with Zig.
- Supported client rates: 45 and 90 FPS.
- Signature resolution has been verified against the Steam 2012 and EA/Origin-
  era 2009 Kane's Wrath 1.02 executables, plus Kane's Wrath 1.03 (2025).
- The Steam and 2009 1.02 binaries are already Large Address Aware. The
  resolver ignores checksum and LAA-header differences, so an otherwise
  identical NTCore-patched executable uses the same code signatures.
- The 2009 and 1.03 builds still need Windows gameplay testing; the Steam 2012
  build remains the primary tested target.
- Tiberium Wars is not supported yet.
- Multiplayer and replay determinism still require runtime validation before
  the patch should be treated as online-safe.

## What it changes

- Raises the complete client/update rate from 30 to 45 or 90 Hz.
- Keeps `g_logicFPS` at 15 and preserves the six-phase logic scheduler.
- Updates the reviewed visual seconds-per-frame and cached-FPS values without
  overwriting the shared `1/30` constant used by pathfinding.
- Uses a 22 ms W3D step at 45 FPS or an 11 ms step at 90 FPS.
- Keeps frame-counted tracers, legacy particles, clouds/lightning, and Anim2D
  on their authored 30 Hz timebase.
- Keeps GPU particle creation and expiration on the same 30 Hz timebase.
- Sets the audio client-frame cache to `1000/targetFPS` milliseconds so
  XML-authored sound delays retain their real-time duration.
- Removes the independent display-side 29 ms wait.
- Optionally replaces the rounded outer limiter with a QPC deadline pacer.
- Reapplies live timing fields after runtime configuration and at each game
  session start.

The patch does not change the simulation rate, replace the phase scheduler,
or support arbitrary display rates such as 60, 120, or 144 FPS.

## Configuration

Copy `kw_fps_patch.ini.example` to `kw_fps_patch.ini`:

```ini
[kw_fps_patch]
enabled=1
target_fps=45
precise_pacing=1
spin_threshold_us=400
logging=1
```

`target_fps` accepts only `45` or `90`. Start with 45 when validating a new
installation.

## Build on macOS

Requirements:

- Zig 0.16 or a compatible `zig cc` release
- GNU or Apple Make
- `objdump` and `file` for static verification

Build and verify the release DLL:

```sh
make verify
```

Create a copy-ready package:

```sh
make package
```

Outputs:

```text
zig-out/bin/dinput8.dll
zig-out/bin/dinput8-debug.dll
zig-out/package/
```

Equivalent Zig commands:

```sh
zig build
zig build verify
zig build -Doptimize=Debug
```

The release DLL is CRT-free, targets the Windows 5.1 subsystem, and imports
only `KERNEL32.dll` and `WINMM.dll` in addition to its DirectInput forwarding
role.

## Automated builds

Every push and pull request produces a commit-specific GitHub Actions artifact:
one ZIP containing `dinput8.dll` and the default INI. Successful commits on
`main` also update the visible
[latest automated build](https://github.com/CNCStuff/cnc3_fps_patch/releases/tag/continuous).

## Install

1. Locate the directory containing the actual `cnc3ep1.dat`, normally
   `RetailExe\1.2`.
2. Place `dinput8.dll` and `kw_fps_patch.ini` beside `cnc3ep1.dat`, not beside
   the launcher executable.
3. Start the game normally through its launcher.
4. Read `kw_fps_patch.log` in the same directory.

The proxy loads the real System32 `dinput8.dll`, forwards its DirectInput and
COM exports, then installs the guarded game patches. Remove the DLL, INI, and
log to uninstall.

A successful installation should log entries resembling:

```text
bootstrap_status=1
dinput8 proxy forwarding initialized
Game hook reached: GameEngine_ApplyRuntimeConfiguration tail
Static visual and limiter patches installed
Frame-counted particles, tracers, clouds and Anim2D pinned to retail 30 Hz
Applied client FPS=45
Applied W3D milliseconds/client-frame=22
```

## Limitations

- 90 FPS runs much of the UI, camera, draw-module, and rendering work three
  times as often as retail, so CPU-bound scenes can still slow down.
- Legacy particle simulation remains at 30 Hz. Rendering occurs at 45/90 Hz,
  but some particle positions can repeat between simulation updates.
- Truck-draw rotation damping is per-update, but shipped XML leaves its
  frame-sensitive default at `1.0`; no speculative patch is installed.
- Every required signature must resolve exactly once, and all derived branch
  targets, operands, globals, and logic-rate invariants must agree before the
  DLL modifies the process.
- Proxy DLLs and runtime code patches may trigger generic antivirus warnings.

See [docs/implementation.md](docs/implementation.md) for patch mechanics and
[docs/testing.md](docs/testing.md) for focused runtime validation.

## License

WTFPL Version 2. See [LICENSE](LICENSE).
