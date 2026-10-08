# Engine notes

Working notes from reverse engineering the Genesis cartridge
(SHA-1 `ccc9542d964de5bdc1e9101620cae40dd98e7127`, 2 MB, checksum 0x9EC6).

Evidence labels used below:

- **[S]** static: read directly from the disassembly.
- **[D]** dynamic: observed in the reference emulator under the tracer.
- **[I]** inferred: a conclusion drawn from the above, not yet confirmed.

Addresses are 68000 addresses. ROM is mapped at 0, work RAM at 0xFF0000.

## Layout

| Range | Contents | Evidence |
|---|---|---|
| 0x000000-0x0001FF | Vectors and cartridge header | S |
| 0x000200-0x00068F | Sega boot code and region-lockout screen | S |
| 0x000690-0x010116 | Engine code with embedded tables (about 53 KB in 300 functions) | S, D |
| 0x010166-0x0C6F20 | Data read by the PackBits decompressor | D |
| 0x01155E-0x136563 | Data read by the LZSS decompressor | D |
| 0x1469C0-0x150597 | Script bytecode read by the interpreter (first episode) | D |
| 0x1BB94A-0x1BD1CF | Block uploaded to the Z80 by the sound code | D |
| 0x1F6D7A-0x1F6F60 | Sound interface code (16 functions) | S, D |

The regions above overlap because they come from one traced session and are
spans, not exact boundaries. Exact resource tables are still to be mapped.

## Boot

- Reset at 0x200 is the standard Sega init. Region check at 0x2FA. Real entry 0x690. [S]
- The vertical and horizontal interrupt vectors jump through RAM pointers at
  0xFF0000 and 0xFF0004, so each screen installs its own handler. [S]
- Video runs in the 256-pixel-wide mode for nearly everything. Only the
  licence screen uses 320. [D]

## Low-level library

| Addr | Name | Behaviour | Ev. |
|---|---|---|---|
| 0x974C | wait_vblank | Sets bit 0 of 0xFF09DC and spins until the interrupt clears it | S |
| 0x9742 | wait_frames | Calls wait_vblank D0+1 times | S |
| 0x97AA | lzss_decode | A0 source, A2 destination, A1 4096-byte ring | S |
| 0x992C | vram_write | D0 address, D1 word count, A0 source | S |
| 0x9964 | vram_poke | Write word D1 at VRAM address D0 | S |
| 0x9996 | vram_peek | Read VRAM word at D0 into D1 | S |
| 0x99C4 | cram_poke | Write one palette word | S |
| 0x99F6 | dma_vram | A0 source, D0 destination, D1 words, split at 64 KB boundaries | S |
| 0x9B10 | dma_cram | Palette upload by DMA | S |
| 0x9BE4 | vram_fill | Fill D1 words at D0 with D2 | S |
| 0x9C58 | vram_clear | vram_fill with zero | S |
| 0x9C64 | packbits_decode | A0 source, A2 destination | S |
| 0x9D34 | read_pads | Pad 1 to 0xFF09E0, pad 2 to 0xFF09E1, active low | S |
| 0x9DE0 | fade_out | Eight steps, reads palette back then darkens | S |
| 0x9E7E | fade_in | Eight steps towards the 64-word palette at A0 | S |
| 0x9F72 | draw_string | Tile text at column D1, row D2 on plane A | S |
| 0xA054 | show_text_top | Centred, word-wrapped dialogue on the window plane | S |
| 0xA236 | strlen | | S |
| 0x22D6 | random | Seed at 0xFF0428, initial 0x45C3F2D1 | S |

### LZSS format (0x97AA) [S]

Big-endian bit stream, most significant bit first.

- Flag bit 1: next 8 bits are a literal byte.
- Flag bit 0: next 12 bits are a ring position. Zero ends the stream.
  Otherwise the next 4 bits are a length code, and length+2 bytes are copied
  from the ring starting at that position.
- The ring is 4096 bytes and the write position starts at 1.
- On exit the decoded length is stored as a long at the start of the ring buffer.

### PackBits format (0x9C64) [S]

- A 32-bit header gives the decoded size. Only the low 16 bits are used.
- Control byte n from 0 to 127: copy n+1 literal bytes.
- Control byte n from 0x80 to 0xFF: repeat the next byte 1-n times as a signed
  value, so 2 to 129 bytes.

### Pad byte layout [S]

Bits from 7 down to 0 are Start, A, C, B, Right, Left, Down, Up. A bit is 0
while pressed.

## Script interpreter

- Two run loops, 0x23DC and 0x2406. Both read a 16-bit opcode at A5, index the
  handler table at 0x2354 and call it. A6 is the end of the script. [S]
- The first loop calls handlers with D0 = 0 and the second with D0 = 1.
  Handlers branch on that flag at entry. The meaning of the two modes is
  still to be confirmed. [S]
- 34 opcodes, 0x00 to 0x21. [S]
- Opcode 0x0C calls one of 62 native engine functions through the table at
  0x2EB4. [S]
- Opcode 0x04 runs a nested block through the function pointer at 0xFF0878. [S]

## Frame interrupt (0xA65C) [S]

When bit 6 of 0xFF09DC is set the handler runs palette cycling (0xA7CE),
uploads changed actor tiles (0xAF24), rebuilds the sprite table at 0xFF0008
and uploads it to VRAM 0xD800 (0xA984, 0xAFAE), then animates two interface
icons (0xA704). It always reads the pads and clears the frame-sync bit.

Sprite table construction order: cursor, optional blinking marker, optional
row of eight panel sprites, optional four fixed sprites, four more fixed
sprites, then up to six actors sorted by depth. Each actor is a grid of
hardware sprites chosen by the frame's width and height codes.

## RAM map (partial)

| Addr | Meaning | Ev. |
|---|---|---|
| 0xFF0000 | Vertical interrupt handler pointer | S |
| 0xFF0004 | Horizontal interrupt handler pointer | S |
| 0xFF0008 | Sprite table buffer, 8 bytes per sprite | S |
| 0xFF0428 | Random seed | S |
| 0xFF068C | Tile attribute base added to text characters | S |
| 0xFF068E | Pointer to the current room header | I |
| 0xFF06AA | Episode index, 0 or 1 | I |
| 0xFF06AE | Object under the cursor, 0 for none, 3 and up for objects | I |
| 0xFF06C0 | Selected verb | I |
| 0xFF06CA, 0xFF06CE | Camera scroll X and Y | I |
| 0xFF06DC, 0xFF06DE | Cursor X and Y | S |
| 0xFF06F8 | Working palette, 64 words | S |
| 0xFF0778 | Target palette, 64 words | I |
| 0xFF080A | Dialogue display timer | S |
| 0xFF080C | Text speed setting | I |
| 0xFF0878 | Pointer to the active script run loop | S |
| 0xFF08BC | Text line buffer | S |
| 0xFF09DC | Engine flags. Bit 0 frame sync, bit 3 cursor enabled, bit 6 game interrupt work enabled | S |
| 0xFF09E0 | Pad 1 state | S |
| 0xFF0AB4 | Active actor mask, six bits | I |
| 0xFF1200 | Object table, 26 bytes per entry | I |

## Port status (2026-10-07)

Checked against the reference emulator:

- The video chip model reproduces nine captured screens with zero differing
  pixels (`build/vdptest`), including the mid-frame panel split.
- Boot screens and the opening animation match pixel for pixel at every
  sampled frame, apart from frame offsets caused by the original's load stalls.
- Title menu: backdrop and text match. The logo differs only in animation phase.
- First room: room, panel and title line match. Differences are confined to
  the two characters and the fireplace, which run on timers and random numbers.

Ported but only checked by eye: script interpreter, natives, walking,
dialogue trees, inventory, password entry.

Not ported:

| Address | What |
|---|---|
| 0x88A2, 0x8C40 | Hidden debug menus (room select, object mover) |

Known deviations from the original:

- The original loses frames while loading. The port does not, so scenes
  start sooner.
- Busy loops that spin on interrupt-updated state give up one frame per pass.
- One script toggle path (0x2D08) uses an unloaded register in the original.
  The port logs it instead of guessing.

The sound test (0x940C) is ported in `menu.cpp`: up and down change the
first hex digit, left and right the second, B plays, C stops, A leaves. Tune
names are at 0x1F7082 and sequence numbers at 0x1F7492, 0x57 entries.

Mouse additions in the title menus, where the system pointer is shown:
pointing at an option moves the cursor to it and a click picks it. On the
password screen a click on a letter or command uses it, a click in the
password text moves the caret, and a right click goes back. In the sound
test the wheel steps through the tunes, left click plays, middle click
stops and right click leaves.

Additions for the mouse: the pointer position is set from the mouse inside
the pointer routine (0x600C), mouse movement enters pointer mode, and a
click on open floor walks the lead there (`actor::clickWalk`).

## Sound

The game's sound interface is six commands pushed through a 64-byte queue in
Z80 RAM (write index at 0x0036, buffer at 0x1B40). The port does not
reimplement the driver. It runs the cartridge's own Z80 program
(0x1BB94A-0x1BD1CF) on a Z80 core with FM and PSG chip models in
`src/core/audio.cpp`, and fills the queue the way the 68000 code does.

Checked against the reference emulator over the first 20 seconds:

- FM register write counts match (key events 217 against 215).
- Energy per frequency band matches within about three points in each band.

One percussion voice runs an operator at 1.5 cycles per sample because of a
detune wrap the chip really has. That gives a tone at half the sample rate,
which the console's analogue stage removes. The port filters its output for
the same reason. Sample playback through the FM chip's DAC has not been
exercised yet, since the stretch tested does not use it.

## Completability checks (2026-10-07)

- `tools/script_scan.py` walks every script in both episodes (init scripts,
  room scripts, 181 and 155 object scripts). All decode cleanly. Every opcode
  and native function they use is implemented, and the one assignment form
  the port does not support is never used.
- `tools/room_smoke.py` enters each of the 21 and 30 rooms and runs it for
  ten seconds. None crash, hang or report a script error. Two rooms in the
  second episode are transitions that move on by themselves.
- `tools/emu/script_trace.py` plus `scooby --script-trace` compare executed
  script instructions with the original. From boot to the first room both run
  the same 730 distinct instructions in the same order of first appearance.
- The arena mini-game (native 0x32) was run with `--room 5 --poke FF2A07=03`
  in the second episode: it starts, the opponent attacks, energy drains, and
  the script continues after a loss.

Fixed along the way, both from Ghidra listings that were misaligned or that I
misread: the walk-to-object helper returns a Y offset as well as an X offset
(0x19AA), and the mini-game's energy bar sprites are four tiles wide with the
link counter advancing by four (0xAB9E).

## Scripted playthroughs (2026-10-07)

Both episodes have been played in the port from boot to the credits and the
restart that follows. The action lists are `tests/playthrough_ep0.txt` (375
actions) and `tests/playthrough_ep1.txt` (256). Each line is
`verb object second`. Replay one with:

    ./build/scooby --replay tests/playthrough_ep0.txt --mute --press 100:S,700:S,1000:S,1300:S,1600:S
    ./build/scooby --replay tests/playthrough_ep1.txt --mute --press 100:S,700:D,760:S,1000:S,1300:S,1600:S

A run that ends with "episode reached its ending" passed.

Replay is strict: an action is refused unless the scripts offer it for an
object in reach at that moment. `tools/minimise_playthrough.py` drops the
actions the ending does not need, giving `tests/solution_ep0.txt` (124
actions) and `tests/solution_ep1.txt` (150). `tools/solution_text.py
solution.txt` turns those into a readable walkthrough using names read from
the ROM; the output is git-ignored.

The lists were found by `--solve` (`src/game/autoplay.cpp`), a search that
snapshots RAM and video state at the main loop's idle point and tries the
verbs the scripts define. They are not short or sensible routes: they repeat
actions and wander. What they show is that a route exists and that every
script on it runs to completion in the port.

Limits of what this proves:

- Actions are injected at the idle point, not clicked. Pointer and panel
  handling are not exercised.
- Text waits return at once, the arena is won by decree after 300 frames,
  the crane fires by itself with a fixed lead, and dialogue lines and the fun
  house direction come from a plan stored in `second` (base 6, lowest digit
  first) for verbs 8 and 11. Verb 15 is "wait four seconds".
- Nothing here compares the port with the original beyond what the earlier
  sections cover.

Bugs found this way and fixed: animation opcode 0x06 for objects stores the
animation number in object 3's first word (0xFF1200), not in the animated
object, and the companion walk at 0x5AD8 needs bit 5 of 0xFF09DE cleared.

### Bed spring softlock

In the first episode, using the bed spring hides the lead for 600 ticks.
The cartridge accepts any verb meanwhile, and a script that walks the lead
then waits for a hidden character to be shown (0x55F0 to 0x5608) and never
returns. Read from the disassembly; not reproduced in the emulator. The port
ends the bounce first, by running the spring's own tick script at its final
count (`endBounceBeforeAction` in `mainloop.cpp`). Looking at things and
taking the lights still happen in mid-air.

Testing aids: `--room N` jumps to a room once the episode has started and
`--poke ADDR=VAL` sets a byte first (story flags are at 0xFF2A00).

## Saved games

An addition; the original has passwords only. A save is work RAM, video RAM,
colour and scroll RAM, the video registers and the whole sound side (Z80
registers and RAM, FM chip, PSG), written by `src/game/savestate.cpp` and
`AudioMachine::saveState`.

The game logic runs on its own thread with a live call stack that cannot be
saved. States are therefore taken and applied only at the adventure loop's
idle point (`savestate::atIdle`), where the stack is always the same. That is
why saving is refused during cutscenes. A game picked on a title screen
makes the menus unwind (`loadWaiting` in `menu.cpp`) and the state is applied
on the first pass of the adventure loop, replacing everything.

The F5 dialog is drawn by the host over the frozen picture
(`src/saveui.cpp`) with a small built-in pixel font (`src/font5x7.h`).
`--dialog-test DIR` renders its screens to PNG files, and `--save-at
FRAME:FILE` and `--load-at FRAME:FILE` exercise saving and loading headless.
