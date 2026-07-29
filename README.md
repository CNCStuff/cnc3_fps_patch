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
   - Kane's Wrath: the directory containing `cnc3ep1.dat`, such as `RetailExe\1.2` or `RetailExe\1.3`.
   - Tiberium Wars: the directory containing `cnc3game.dat`, such as `RetailExe\1.9` or `RetailExe\1.10`.
   If you use multiple versions, place the DLL and INI files into each version's
   directory.

3. That's it! Now you can start the game and go into the campaign or skirmish, and the game will render at 90 FPS! If it doesn't, you can check the file named `fps_patch.log` created in the same folder with the .dll. If the `.log` file doesn't appear, you most likely put the `.dll` in the wrong folder - not the one with the game version that you're actually using. Remember to put the `.dll` and `.ini` next to the `.dat` file, not the main launcher `.exe`.

## Configuration

The release ZIP includes `fps_patch.ini` with the default 90 FPS and some other settings configured (including logging).
You can open it in any text editor and select 45, 60, 75, or 90 FPS. For example:

```ini
target_fps=45
```

## Current status

Campaign and skirmish should play very smoothly without any major issues.

The patch explicitly handles:

- Animations.

- Particle effects. Note: they are authored to play at 30 FPS, so they might look a bit more "stuttery" with the patch when rendered at 90 FPS. This isn't something that the patch can fix without actually changing the particle effects.

- Ability decal throbbing.

- Audio delay for everything, including normal units, special powers, short sound effects - e.g. money ticking.

- Camera input via edge scrolling, RMB drag scrolling, arrow keys, rotation.

- Key repetition delay in the chat for keys such as Backspace.

## Multiplayer

From current limited testing multiplayer is **unstable**: some matches play fine end to end, but in some longer games the game might desynchronize.

So, for now: only use this patch in multiplayer **at your own risk**, and avoid using it in tournaments or other high-stake games.

## Issues

Please report any issues, such as crashes, animations or any other effects or gameplay going faster or slower than expected to https://github.com/CNCStuff/cnc3_fps_patch/issues. Feedback is appreciated!

Windows 7 users who are missing the Universal CRT can install
[Microsoft's Universal C Runtime update](https://support.microsoft.com/en-us/servicing/os/windows/2020/06/update-for-universal-c-runtime-in-windows).

## Future directions

This patch could likely be adapted to Red Alert 3 because the games share the same engine, with little evolution between them.

## License

WTFPL Version 2. See [LICENSE](LICENSE).
