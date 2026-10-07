// The game's sound interface (0x1F6D7A-0x1F6F60). The cartridge uses the GEMS
// driver: a Z80 program fed through a 64-byte command queue. The engine side
// only ever issues the six commands below, so that is the seam the
// reimplementation keeps. The backend is pluggable; the default one only
// records commands.
#pragma once
#include "../core/types.h"
#include <functional>

namespace snd {

struct Command {
    enum Kind { Init, StartSequence, StopSequence, PauseAll, ResumeAll, StopAll } kind;
    u32 arg[4];
};

// Receives every command the engine issues. Unset by default.
extern std::function<void(const Command&)> sink;

void init(u32 patches, u32 envelopes, u32 sequences, u32 samples);   // 0x1F6EA8
void startSequence(u32 id);   // 0x9762 -> 0x1F6EF8, command 0x10
void stopSequence(u32 id);    // 0x9776 -> 0x1F6F16, command 0x12
void pauseAll();              // 0x1F6F2A, command 0x0C
void resumeAll();             // 0x1F6F3E, command 0x0D
void stopAll();               // 0x1F6F52, command 0x16

}  // namespace snd
