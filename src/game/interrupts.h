// Frame and line interrupt work. The original installs handler addresses in
// RAM; the reimplementation keeps those values and dispatches on them.
#pragma once
#include "../core/types.h"

namespace irq {

enum : u32 {
    VblMain  = 0xA65C,   // gameplay and boot screens
    VblPlain = 0xA4A4,   // pads and timers only
    VblTitle = 0xA38C,   // title screen with the lightning flash
    HblNone  = 0x1006A,
    HblSplit = 0x1006C,  // switches to the verb-panel planes at line 168
};

void vblank();            // run once per presented frame, before the game slice
void beforeLine(int y);   // run by the renderer before each scan line
void updateScroll();      // 0xFA78

}  // namespace irq
