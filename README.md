# Scooby-Doo Mystery, native reimplementation

A reverse-engineered, native reimplementation of the engine of the Sega
Genesis game, built on SFML 3. It needs your own copy of the cartridge image:
all graphics, text and scripts are read from it at run time, and none of it
is stored in this repository.

## Build and run

You need three things: Git, CMake 3.24 or newer, and a C++20 compiler. The
build downloads everything else by itself (SFML 3 and the two sound cores),
so the first configure needs an internet connection and takes a few minutes.

### 1. Install the tools

**macOS**

```bash
xcode-select --install        # compiler and Git
brew install cmake            # from https://brew.sh
```

**Linux (Debian, Ubuntu, Mint)**

```bash
sudo apt install git cmake g++ libx11-dev libxrandr-dev libxcursor-dev libxi-dev \
  libudev-dev libgl1-mesa-dev libfreetype-dev libflac-dev libvorbis-dev libogg-dev
```

On Fedora the same libraries are `git cmake gcc-c++ libX11-devel
libXrandr-devel libXcursor-devel libXi-devel systemd-devel mesa-libGL-devel
freetype-devel flac-devel libvorbis-devel libogg-devel`. On Arch:
`git cmake gcc libx11 libxrandr libxcursor libxi systemd-libs mesa freetype2
flac libvorbis libogg`.

**Windows**

Install [Visual Studio 2022 Community](https://visualstudio.microsoft.com/)
with the "Desktop development with C++" workload (it includes CMake), and
[Git for Windows](https://git-scm.com/download/win). Run the commands below
in the "Developer PowerShell for VS 2022" from the Start menu.

### 2. Get the source and build

The same on every platform:

```bash
git clone --recursive <this repository's URL> scooby
cd scooby
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The program ends up at `build/scooby` (`build\scooby.exe` on Windows).

### 3. Add your ROM and play

You need your own dump of the USA release of the cartridge: a plain 2 MB
file, usually ending in `.md`, `.bin` or `.gen`. If it is in a zip, unzip it
first. Then give its path to the program:

```bash
./build/scooby "path/to/Scooby-Doo Mystery (USA).md"
```

```powershell
build\scooby.exe "C:\path\to\Scooby-Doo Mystery (USA).md"
```

On Windows you can also drag the ROM file onto `scooby.exe`. If you put the
file at `rom/Scooby-Doo Mystery (USA).md` inside the source folder and run
the program from there, no argument is needed.

Options: `--fullscreen` starts in fullscreen, `--scale N` sets the window
size, `--mute` turns sound off. The picture is always shown 4:3 and centred,
with black bars where the display is wider.

### If something goes wrong

| Message | Meaning |
|---|---|
| `cannot open ...` | The path to the ROM is wrong. Put it in quotes if it has spaces |
| `unexpected ROM size` | The file is not a plain 2 MB image. It may still be zipped, or be an interleaved `.smd` dump |
| `checksum mismatch` | It is a different release or a modified ROM. Only the USA release works |
| CMake cannot find a package on Linux | One of the libraries in step 1 is missing |

Tested on macOS (Apple Silicon, Clang) and Ubuntu 24.04 (GCC 13). The
Windows steps have not been tried on a real machine yet; please report what
breaks.

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
