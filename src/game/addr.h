// Work-RAM locations used by the engine, named as their purpose became clear.
// Addresses are those of the original program so the cartridge's scripts,
// which refer to engine state by absolute address, keep working.
#pragma once
#include "../core/types.h"

enum : u32 {
    VBlankHandler   = 0xFF0000,   // long: address of the active frame-interrupt routine
    HBlankHandler   = 0xFF0004,   // long: address of the active line-interrupt routine
    SpriteBuffer    = 0xFF0008,   // sprite table staging, 8 bytes per entry
    RandomSeed      = 0xFF0428,
    TextAttrBase    = 0xFF068C,   // word added to every text character
    RoomHeaderPtr   = 0xFF068E,
    EpisodeIndex    = 0xFF06AA,
    CameraX         = 0xFF06CA,
    CameraY         = 0xFF06CE,
    CursorX         = 0xFF06DC,
    CursorY         = 0xFF06DE,
    WorkPalette     = 0xFF06F8,   // 64 words
    TargetPalette   = 0xFF0778,   // 64 words
    ScrollAX        = 0xFF07F8,
    ScrollBX        = 0xFF07FA,
    ScrollAY        = 0xFF07FC,
    ScrollBY        = 0xFF07FE,
    EngineFlags     = 0xFF09DC,   // bit 0 frame wait, bit 3 cursor on, bit 6 interrupt work enabled
    PadState        = 0xFF09E0,   // pad 1, pad 2 (active low)
    PadPrev         = 0xFF09E2,
    PadLatch        = 0xFF09E4,
    PadEdge         = 0xFF09E6,
};
