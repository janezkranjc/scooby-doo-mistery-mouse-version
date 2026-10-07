// Room loading, entry scripts, collision, object patches and scrolling.
#pragma once
#include "../core/types.h"

namespace room {

void enter();          // 0x2138: load the room in 0xFF06AC and run its entry script
void enterAndShow();   // 0x2196: enter, then fade in
void show();           // 0x219A: fade in and run the after-entry script
void load();           // 0x758A
void restorePanel();   // 0x7950: redraw both planes from the room maps
void resetActors();    // 0x7A8C
void objectPass();     // 0x7C04: per-frame object timers and redraws
void scrollUpdates();  // dispatches 0xFC3A / 0xFD5E / 0xFE74 / 0xFF68 from 0xFF0AB3

// Collision. Both test one pixel against the room's collision map.
bool blockedProbe();                 // 0x7476: point at 0xFF064A / 0xFF064E
bool blockedTrial(u16 maxX, u16 maxY);   // 0x73D6: point at 0xFF0642 / 0xFF0646, with bounds

}  // namespace room
