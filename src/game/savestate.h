// Saved games. A save is the whole machine at the adventure loop's idle
// point: work RAM, video memory and registers, and the sound side. The game
// logic runs on its own thread with a live call stack, which cannot be
// saved, so states are only taken and applied at that one point, where the
// stack is always the same.
#pragma once
#include "../core/types.h"
#include <string>
#include <vector>

namespace savestate {

constexpr size_t kNameLength = 32;

struct Info {
    std::string name;
    int episode = 0, room = 0;
};

std::vector<u8> capture();                         // game thread, at the idle point
bool apply(const std::vector<u8>& data);           // game thread, at the idle point
bool read(const std::vector<u8>& data, Info& out); // checks a file and reads its label
void setName(std::vector<u8>& data, const std::string& name);

void atIdle();   // called by the adventure loop whenever the player has control

}  // namespace savestate
