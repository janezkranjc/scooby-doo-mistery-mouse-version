// Title screen and its menus (0x8196-0x96F3).
#pragma once
#include "../core/types.h"

namespace menu {

struct Restart {};   // thrown when the player quits to the start of the program

// 0x8196. Returns once a game is to be started or resumed. Episode, room and
// flag state are left in work RAM exactly as the original leaves them.
void titleMenu();

void makePassword();   // 0x8660: writes the two password lines at 0xFF08BC
void titleFrameWork(); // 0x879E: per-frame logo bob and wave
void titleFlash();     // 0xA38C-0xA4A0: lightning on the title backdrop

}  // namespace menu
