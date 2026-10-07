// Entry points of the reimplemented game program.
#pragma once
#include "../core/types.h"

namespace game {

void entry();          // 0x690: runs on the game thread and never returns normally
void bootScreens();    // 0x8CE: publisher and licence screens
void titleAndRun();    // 0xBF4: opening animation, menu, then the adventure loop

}  // namespace game
