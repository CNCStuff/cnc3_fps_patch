# Command & Conquer 3: Tiberium Wars and Kane's Wrath FPS patch

[![Build DLL](https://github.com/CNCStuff/cnc3_fps_patch/actions/workflows/build.yml/badge.svg?branch=main&event=push)](https://github.com/CNCStuff/cnc3_fps_patch/actions/workflows/build.yml?query=branch%3Amain+event%3Apush)

This project is a DLL-based patch that changes multiple functions and constants
in the Tiberium Wars or Kane's Wrath binary at runtime to make it possible to
render the game at **45, 60, 75, or 90 FPS** compared to the default **30 FPS**.
The actual simulation clock still ticks at the default 15Hz.

The patch is loaded through a DLL named `dinput8.dll` so the game loads it
automatically. The proxy forwards DirectInput calls to the system DLL.

Reverse engineering of the game binary, the implementation and documentation were heavily LLM-assisted.

## Installation
1. Download the newest release zip from [the automated builds](https://github.com/CNCStuff/cnc3_fps_patch/releases/latest)

2. Find the directory with the game module that you want to patch:
   - Kane's Wrath: the directory containing `cnc3ep1.dat`, such as `RetailExe\1.2`.
   - Tiberium Wars: the directory containing `cnc3game.dat`, such as
     `RetailExe\1.9` or `RetailExe\1.10`.
   If you use multiple versions, place the DLL and INI files into each version's
   directory.

3. That's it! Now you can start the game and go into the campaign or skirmish, and the game will render at 90 FPS! If it doesn't, you can check the file named `fps_patch.log` created in the same folder with the .dll and the .ini. If the `.log` file doesn't appear, you most likely put the `.dll` in the wrong folder - not the one with the game version that you're actually using. Remember to put the `.dll` and `.ini` next to the `.dat` file, not the main launcher `.exe`.

## Configuration

The release ZIP includes `fps_patch.ini` with the default 90 FPS and some other settings configured (including logging).
You can open it in any text editor and select 45, 60, 75, or 90 FPS. For example:

```ini
target_fps=45
```

## Current status
There was limited gameplay testing, but so far:

- All animations play at the same usual speed.

- Particle effects, ability decals work at normal speed. A lot of particle effects are authored to play at 30 FPS, so they might look a bit more "stuttery" even if they render at 90 FPS. This isn't something that the patch can fix directly.

- Audio delay is properly adjusted so that it matches with actual gameplay and visual effects.

## Known issues

Some camera controls are too fast at higher FPS:

- Edge scrolling
- Zoom
- Arrow keys

### Multiplayer
No proper multiplayer testing was done yet, so it'll be appreciated if you try! In theory, the patch should **not** cause any desyncs, and moreover, should work fine if only one player has the patch, while others play with the vanilla game. This is because the actual game logic still runs at the same speed.

But be aware that this is still beta-quality software: do not yet use it in important matches, official competitions and so on.

## Issues

Please report any issues, such as crashes, animations or any other effects/gameplay going faster/slower than expected to https://github.com/CNCStuff/cnc3_fps_patch/issues. Feedback is appreciated!

## License

WTFPL Version 2. See [LICENSE](LICENSE).
