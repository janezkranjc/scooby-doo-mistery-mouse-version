# Scooby-Doo Mystery, native reimplementation

A reverse-engineered, native reimplementation of the engine of the Sega
Genesis game, built on SFML 3. It needs your own copy of the cartridge image:
all graphics, text and scripts are read from it at run time, and none of it
is stored in this repository.

## Build and run

Requires CMake, a C++20 compiler and SFML 3 (`brew install sfml cmake ninja`).

```bash
git submodule update --init   # sound chip and Z80 cores in third_party/
cmake -S . -B build -G Ninja
cmake --build build
./build/scooby --rom "rom/Scooby-Doo Mystery (USA).md"
```

The ROM must be the USA release (2 MB, header checksum 9EC6). `--scale N`
sets the window size and `--fullscreen` starts in fullscreen. The picture is
always shown 4:3 and centred, with black bars where the display is wider.

## Controls

| Input | Action |
|---|---|
| Mouse move | Moves the pointer |
| Left click | Pick a verb, or use the chosen verb on an object. With no verb chosen, walk to the clicked spot |
| Right click | On a hotspot, run its suggested verb. Elsewhere, drop the chosen verb |
| Click the panel's arrow button, middle click, or D | Swap the verb panel and the inventory |
| Title menus | Point at an option and click it. On the password screen click letters and commands, click in the password to move the caret, right click to go back |
| Sound test | Mouse wheel picks the tune, left click plays, middle click stops, right click leaves. On the pad: arrows pick, B plays, C stops, A leaves |
| Arrow keys | Walk directly, or move the pointer |
| A / S / D, or Z / X / C | Pad buttons A / B / C |
| Enter | Start: confirm in menus, pause in game |
| F11, F or Alt+Enter | Toggle fullscreen at the desktop resolution |
| Esc | Leave fullscreen, or quit when windowed |

## Status

Working: boot screens, title menu, password screen, sound test, episode intro, rooms,
scrolling, the script interpreter, actors with depth scaling, walking, the
verb panel, inventory, speech and dialogue trees, mouse pointer, and sound.
Sound runs the cartridge's own driver program on a Z80 core with FM and PSG
chip models (`third_party/`, MIT and BSD licences). `--mute` turns it off.

Not done yet: the hidden debug menus, which are not needed to play. See `docs/` for details.

## Layout

- `src/core` machine model: memory, the software video chip, frame hand-off.
- `src/game` the engine, ported routine by routine from the 68000 code.
- `tools` analysis scripts: Ghidra exports, disassembly viewers, and the
  reference-emulator harness used to check the port.
- `docs` notes on the engine and the script interpreter.
- `tests/vdptest.cpp` checks the video chip model against emulator captures.
