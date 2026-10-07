// The script interpreter (0x23DC-0x2EB2, 0x48B6-0x5000) and the native
// functions scripts can call (table at 0x2EB4). See docs/SCRIPT_VM.md.
#pragma once
#include "../core/types.h"
#include <functional>

namespace vm {

// Interpreter registers. The original keeps these in A5, A6 and D7 across
// every call, so they are shared state here too.
extern u32 pc;     // A5: current instruction
extern u32 end;    // A6: end of the current block
extern u16 verb;   // D7: verb being matched by verb headers

void runScan();    // 0x23DC: find the block header that applies
void runExec();    // 0x2406: carry out actions
void runActive();  // through the loop selected at 0xFF0878

enum : u32 { ScanLoop = 0x23DC, ExecLoop = 0x2406 };

// Optional observer called before every instruction (address, opcode, exec?).
extern std::function<void(u32, u16, bool)> trace;

// Bit of the actor masks for a script "who" operand: 1 and 2 are the two
// lead characters, 3 and up are objects that currently own an actor slot.
int actorBit(u16 who);
// Address of the object-table entry for a script object number (3 and up).
u32 objectAddr(u16 number);

}  // namespace vm
