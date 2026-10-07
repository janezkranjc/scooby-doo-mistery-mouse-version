// Actor animation, movement and per-frame object scripts.
#pragma once
#include "../core/machine.h"
#include "lowlevel.h"

namespace actor {

void update();        // 0xB0B8: advance every actor's animation program
void objectTicks();   // 0xAFDA: run armed per-frame object scripts
void initLeads();     // 0x801A

// The original spins on update() while the frame interrupt moves timers on.
// Here each pass that has not reached its condition gives up one frame.
template <class Done>
void spin(Done done) {
    for (;;) {
        update();
        if (done()) return;
        ll::idleFrame();
    }
}

void walkLeadTo(u16 x, u16 y, u16 mode, u16 anim);          // 0x5002
void walkCompanionTo(u16 x, u16 y, u16 mode, u16 anim);     // 0x566A
void companionWalkStart(u16 x, u16 y, u16 mode, u16 anim);  // 0x5AD8
u16 companionWalk(u16 x, u16 y, u16 mode, u16 anim, bool& started);   // 0x56B0
void moveObjectActor(u16 who, u16 x, u16 y, s16 mode, u16 anim);   // 0x4B24
u16 standOffset(u32 animBase, u16 index, u16* yOffset = nullptr);   // 0x197E: frame words +0x0E and +0x10

// Addition for pointer devices: walk the lead towards a clicked point, as far
// as the room's collision map and gateways allow.
void clickWalk(int x, int y, int object = 0);

}  // namespace actor
