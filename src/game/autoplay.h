// Solver mode (a testing tool, not part of the game): plays an episode by
// search. At each idle point of the main loop it snapshots the machine, tries
// the actions the scripts define for objects in reach, keeps going from any
// state it has not seen, and backtracks otherwise. It ends when the episode
// restarts the program, which is what the ending does.
#pragma once
#include "../core/types.h"
#include <string>

namespace autoplay {

extern bool active;            // waits for input return at once while set
extern bool done;              // the search has ended (see `result`)
extern std::string result;
extern u32 idleCalls;          // bumped on every idle point, for the host's stall check

void loadReplay(const std::string& file, bool thenSearch);   // lines of "verb object second"
int chooseLine(int count);     // which of `count` open dialogue lines to say
void idle();                   // called at the top of the adventure loop
struct Abort {};               // thrown out of a frame wait to abandon an action that never returns
extern bool abortRequested;
void checkAbort();             // called after every frame wait
void aborted();                // called by the main loop after catching Abort
void stalled();                // called by the host when no idle point comes
void programRestarted();       // called when the game restarts itself

}  // namespace autoplay
