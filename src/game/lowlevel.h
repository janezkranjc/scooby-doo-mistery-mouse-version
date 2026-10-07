// Ports of the cartridge's low-level library (0x96F4-0xA3FF).
// Each function notes the address of the routine it reimplements.
#pragma once
#include "../core/machine.h"

namespace ll {

struct SkipScreens {};             // thrown from a frame wait while `allowSkip` is set
extern bool allowSkip;             // addition: lets a button press cut the boot screens short
void waitVBlank();                 // 0x974C
void waitFrames(u16 count);        // 0x9742: waits count+1 frames
void idleFrame();                  // one frame of a busy-wait on interrupt-updated state

// Video memory. Sources are bus addresses (ROM or RAM). Functions that the
// original leaves pointing past the data return the advanced source address.
u32 vramWrite(u16 addr, u16 words, u32 src);   // 0x992C
void vramPoke(u16 addr, u16 value);            // 0x9964
u16 vramPeek(u16 addr);                        // 0x9996
void cramPoke(u16 addr, u16 value);            // 0x99C4
u32 dmaVram(u32 src, u16 addr, u16 words);     // 0x99F6
u32 dmaCram(u32 src, u16 addr, u16 words);     // 0x9B10
void vramFill(u16 addr, u16 words, u16 value); // 0x9BE4
void cramFill(u16 addr, u16 words, u16 value); // 0x9C1E
void vramClear(u16 addr, u16 words);           // 0x9C58
void cramClear(u16 addr, u16 words);           // 0x9C5E
void setVdpReg(u16 reg, u16 value);            // 0x972E

// Decompressors. Both write into work RAM.
void lzssDecode(u32 src, u32 ring, u32 dst);   // 0x97AA: decoded size is left as a long at `ring`
void packbitsDecode(u32 src, u32 dst);         // 0x9C64

// Palette fades over the working palette at 0xFF06F8.
void fadeOut();                    // 0x9DE0
void fadeIn(u32 targetPalette);    // 0x9E7E

void readPads();                   // 0x9D34
u16 random(u16 range);             // 0x22D6
u16 strLen(u32 s);                 // 0xA236
u32 drawString(u32 s, u16 col, u16 row);        // 0x9F72: returns the address after the terminator
u32 drawStringHigh(u32 s, u16 col, u16 row);    // 0x9F58: same, with the priority bit set
u32 drawStringCleared(u32 s, u16 col, u16 row); // 0x9F46: blanks the row first
void loadFont(u16 fg, u16 bg, u16 attrBase);   // 0x7A2A
void showPackedScreen(u32 src);                // 0x96F4

}  // namespace ll
