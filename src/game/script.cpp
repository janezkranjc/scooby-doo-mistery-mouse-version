#include "script.h"
#include "actors.h"
#include "addr.h"
#include "lowlevel.h"
#include "room.h"
#include "sound.h"
#include "ui.h"
#include <cstdio>

namespace vm {

u32 pc = 0, end = 0;
u16 verb = 0;
std::function<void(u32, u16, bool)> trace;

namespace {

constexpr u32 kFlags = 0xFF2A00;      // story flags, bit-addressed
constexpr u32 kObjects = 0xFF1200;
constexpr u32 kVmFlags = 0xFF0AC9;

inline u16 arg(int off) { return R16(pc + off); }
inline s16 sarg(int off) { return RS16(pc + off); }

// Register-numbered bit operations on memory use the bit number modulo 8.
inline bool btstReg(u32 addr, int bit) { return BTST(addr, bit & 7); }
inline void bsetReg(u32 addr, int bit) { BSET(addr, bit & 7); }
inline void bclrReg(u32 addr, int bit) { BCLR(addr, bit & 7); }

// Object field addressed the way the scripts do it: byte offset plus object
// number, rounded down to a word boundary.
inline u32 fieldAddr(u16 offset, u16 number) {
    const u16 idx = u16((u16(u32(u16(number - 3)) * 0x1A) + offset) & 0xFFFE);
    return kObjects + s16(idx);
}

// 0x2796
bool condition() {
    const u16 flags = arg(0x0E);
    s16 lhs, rhs;
    if (flags & 1) lhs = btstReg(kFlags + sarg(6), arg(8)) ? 1 : 0;
    else if (flags & 2) lhs = (arg(8) == 1 && arg(6) == 6) ? RS16(0xFF06AC) : RS16(fieldAddr(arg(6), arg(8)));
    else lhs = sarg(6);
    if (flags & 4) rhs = btstReg(kFlags + sarg(0x0A), arg(0x0C)) ? 1 : 0;
    else if (flags & 8) rhs = RS16(fieldAddr(arg(0x0A), arg(0x0C)));
    else rhs = sarg(0x0A);
    if (flags & 0x10) return lhs == rhs;
    if (flags & 0x20) return lhs > rhs;
    if (flags & 0x40) return lhs < rhs;
    return lhs != rhs;
}

// ---- block headers -----------------------------------------------------

void opTickHeader(bool exec) {   // 0x13
    if (!exec && BTST(kVmFlags, 4)) { BSET(kVmFlags, 0); return; }
    pc += sarg(2);
}

void opVerbHeader(bool exec) {   // 0x01
    auto skip = [] { pc += sarg(6); };
    if (exec || !BTST(kVmFlags, 1) || verb != arg(2)) return skip();
    bool twoObjects = false;
    if (verb == 5) { BCLR(0xFF09E8, 0); twoObjects = true; }
    else if (verb == 6) { BSET(0xFF09E8, 0); twoObjects = true; }
    if (twoObjects && arg(4) != 0) {
        if (R16(0xFF06B6) == 0) {
            BSET(kVmFlags, 5);
            if (!BTST(0xFF09DE, 0)) {
                BSET(0xFF09DE, 0);
                W16(0xFF06B6, 0);
                return skip();
            }
            if (R16(0xFF06B6) == 0) return skip();
        }
        if (verb != arg(2) || R16(0xFF06B6) != arg(4)) return skip();
    }
    BSET(kVmFlags, 0);
}

void opChoiceHeader(bool exec) {   // 0x02
    if (!exec && BTST(kVmFlags, 2)) {
        const u32 text = R32(R32(RoomHeaderPtr) + 0x20);
        const u16 n = R16(0xFF080E);
        if (s16(n) < 5) {
            W32(0xFF0838 + n * 4, text + R32(pc + 2));
            W32(0xFF084C + n * 4, text + R32(pc + 6));
            W16(0xFF082E + n * 2, u16(arg(0x0A) - 0x0E));
            W16(0xFF0824 + n * 2, arg(0x0C));
            W32(0xFF0860 + n * 4, pc + 0x0E);
            W16(0xFF080E, u16(n + 1));
        }
    }
    pc += sarg(0x0A);
}

// ---- flow --------------------------------------------------------------

void opIf(bool) {   // 0x03
    if (condition()) pc += 0x10;
    else pc += sarg(2);
}

void opIfBlock(bool) {   // 0x04
    if (!condition()) { pc += sarg(2); return; }
    const u32 savedPc = pc, savedEnd = end;
    end = pc + sarg(2);
    pc += 0x10;
    runActive();
    if (BTST(kVmFlags, 0)) return;   // a header matched inside: leave pc and end on it
    const u32 after = pc;
    pc = savedPc;
    end = savedEnd;
    pc = after + sarg(4);
}

// ---- state -------------------------------------------------------------

void opCopyAnimDone(bool exec) {   // 0x1D
    if (exec) {
        BCLR(kFlags, 3);
        if (btstReg(0xFF0ABC, actorBit(arg(2)))) BSET(kFlags, 3);
    }
    pc += 4;
}

void opCopyWalking(bool exec) {   // 0x1C
    if (exec) {
        BCLR(kFlags, 2);
        if (btstReg(0xFF0AB7, actorBit(arg(2)))) BSET(kFlags, 2);
    }
    pc += 4;
}

void opPaletteCycle(bool exec) {   // 0x1A
    if (exec && sarg(2) <= 4) {
        const u16 slot = arg(2);
        if (sarg(8) < 0) {
            bclrReg(0xFF0ACD, slot);
            const u16 first = arg(4);
            ll::dmaCram(TargetPalette + u32(first) * 2, u16(first * 2), u16(arg(6) - first));   // byte offset, as at 0x2654
        } else {
            W16(0xFF087C + slot * 2, arg(4));
            W16(0xFF0886 + slot * 2, arg(6));
            W16(0xFF089A + slot * 2, arg(8));
            W16(0xFF0890 + slot * 2, arg(8));
            bsetReg(0xFF0ACD, slot);
        }
    }
    pc += 10;
}

void opFace(bool exec) {   // 0x1E
    if (exec) {
        const u16 who = arg(2) & 0x7FFF;
        const u16 facing = arg(4);
        const bool wait = sarg(2) < 0;
        if (who == 1) {
            const u16 idx = u16((R16(0xFF05B8) << 3) + facing * 2);
            W16(0xFF05B8, facing);
            W16(0xFF0488, R16(0xFC740 + idx));
            BSET(0xFF0AB5, 0);
            if (wait) actor::spin([] { return BTST(0xFF0ABC, 0); });
        } else {
            const u16 idx = u16((R16(0xFF05BA) << 3) + facing * 2);
            W16(0xFF05BA, facing);
            W16(0xFF048A, R16(0xFC700 + idx));
            if (wait) {
                BSET(0xFF0AB5, 1);
                actor::spin([] { return BTST(0xFF0ABC, 1); });
                W16(0xFF05BA, facing);
            }
        }
    }
    pc += 6;
}

void opAssign(bool exec) {   // 0x08
    if (exec) {
        const u16 flags = arg(0x0A);
        u16 value;
        if (flags & 4) value = btstReg(kFlags + sarg(6), arg(8)) ? 1 : 0;
        else if (flags & 8) value = R16(fieldAddr(arg(6), arg(8)));
        else {
            value = arg(6);
            if (flags & 0x80) value = ll::random(value);
        }
        if (flags & 1) {
            const u32 a = kFlags + sarg(2);
            if (flags & 0x10) {
                if (value) bsetReg(a, arg(4));
                else bclrReg(a, arg(4));
            } else {
                // The original toggles through an address register that this
                // handler never loads (0x2D08). No script has been seen to
                // reach it; report it rather than guess.
                std::fprintf(stderr, "script: unsupported flag toggle at %06X\n", pc);
            }
        } else {
            const u32 a = fieldAddr(arg(2), arg(4));
            if (flags & 0x10) W16(a, value);
            else if (flags & 0x20) W16(a, u16(R16(a) - value));
            else if (flags & 0x40) W16(a, u16(R16(a) | value));
            else W16(a, u16(R16(a) + value));
        }
    }
    pc += 12;
}

void opSetObjectByte(bool exec) {   // 0x09
    if (exec) {
        const u16 number = arg(2);
        const u16 idx = u16(u16(u32(u16(number - 3)) * 0x1A) + RS8(pc + 5));
        const u32 a = kObjects + 0x16 + s16(idx);
        if (number == R16(0xFF06AE)) {
            W16(0xFF06C2, u16(s16(RS8(a))));
            ui::highlightVerb(0);
        }
        W8(a, R8(pc + 4));
        if (R16(0xFF06C0) != 0) {
            W16(0xFF06C2, R16(0xFF06C0));
            W16(0xFF06C0, 0);
        }
    }
    pc += 6;
}

void opSetObjectIcon(bool exec) {   // 0x0A
    if (exec) {
        const u32 o = objectAddr(arg(2));
        W16(o + 4, arg(4));
        if (R16(o + 6) == 1) {
            ll::waitVBlank();
            ui::redrawInventory();
        }
    }
    pc += 6;
}

void opSetObjectState(bool exec) {   // 0x0B
    if (exec) {
        const u32 o = objectAddr(arg(2) & 0x7FFF);
        const u16 state = arg(4);
        if (BTST(o + 0x18, 1)) {
            W16(o, state);
        } else {
            const u16 room = R16(o + 6);
            if (state != 0) W16(o + 2, state);
            if (room != R16(0xFF06AC)) {
                W16(o, state);
            } else {
                bool redraw = true;
                if (state == 0) {
                    if (R16(o) == 0) redraw = false;
                    else W16(o, R16(o) | 0xC000);
                } else {
                    W16(o, state);
                }
                if (redraw) {
                    W8(o + 0x18, R8(o + 0x18) | 1);
                    if (sarg(2) < 0)
                        while (BTST(o + 0x18, 0)) ll::idleFrame();
                }
            }
        }
    }
    pc += 6;
}

void opSetObjectHotspot(bool exec) {   // 0x12 and 0x14
    if (exec) W16(objectAddr(arg(2)) + 2, arg(4));
    pc += 6;
}

void opRedrawObject(bool exec) {   // 0x07
    if (exec) {
        const u32 o = objectAddr(arg(2));
        if (R16(o + 6) == R16(0xFF06AC)) {
            W8(o + 0x18, R8(o + 0x18) | 1);
        } else if (R16(o + 6) == 1) {
            u16 count = 0;
            for (u32 p = 0xFF0A1E; RS16(p) >= 0; p += 2) count++;
            count >>= 2;
            if (count != u16(R16(0xFF06EC) - 1)) {
                W16(0xFF06EC, count);
                ui::redrawInventory();
            }
        }
    }
    pc += 4;
}

void removeFromInventory(u16 index) {
    u32 p = 0xFF0A1E;
    while (R16(p) != index) p += 2;
    p += 2;
    for (;;) {
        const u16 v = R16(p);
        W16(p - 2, v);
        p += 2;
        if (s16(v) < 0) break;
    }
}

void opMoveObject(bool exec) {   // 0x05
    const auto done = [] { pc += 6; };
    if (!exec) return done();
    const u16 number = arg(2) & 0x7FFF;
    const bool quiet = sarg(2) < 0;
    if (number <= 2) return done();
    const u32 o = objectAddr(number);
    const u16 here = R16(0xFF06AC);
    const u16 dest = arg(4);

    if (BTST(o + 0x18, 1)) {   // a character
        if (R16(o + 6) == here) {
            const int bit = (R8(o + 0x19) + 2) & 0xFF;
            bclrReg(0xFF0AB4, bit);
            bclrReg(0xFF0AB6, bit);
            W8(o + 0x19, 0xFF);
        }
        W16(o + 6, dest);
        if (dest != here) return done();
        int slot = 2;
        while (BTST(0xFF0AB4, slot))
            if (++slot == 6) return done();
        // Graphics sets are found by id in a table of 36 pointers.
        const u16 id = R16(o + 2);
        u32 base = 0, entry = 0x32670;
        for (int i = 0; i < 0x24; i++, entry += 4)
            if (R16(R32(entry)) == id) { base = R32(entry); break; }
        if (!base) return done();
        const u32 speed = R32(entry + 4 - 0x94);
        W32(0xFF0514 + slot * 4, speed);
        W32(0xFF0528 + slot * 4, speed >> 1);
        W32(0xFF0470 + slot * 4, base);
        W16(0xFF0488 + slot * 2, R16(o));
        W16(0xFF04DC + slot * 4, R16(o + 0x12));
        W16(0xFF04F4 + slot * 4, R16(o + 0x14));
        W16(0xFF0618 + slot * 4, 0xFFFF);   // indexed by slot*4 in the original
        W16(0xFF05DC + slot * 2, R16(o + 0x12));
        W16(0xFF05E8 + slot * 2, R16(o + 0x14));
        W8(o + 0x19, u8(slot - 2));
        BCLR(0xFF0AB8, slot);
        if (BTST(o + 0x18, 3)) BSET(0xFF0AB8, slot);
        BCLR(0xFF0AB6, slot);
        BCLR(0xFF0AB9, slot);
        BCLR(0xFF0ABD, slot);
        BSET(0xFF0AB5, slot);
        BSET(0xFF0AB4, slot);
        actor::spin([slot] { return BTST(0xFF0ABD, slot); });
        W16(o + 6, dest);
        return done();
    }

    const u16 from = R16(o + 6);
    if (from == here && !quiet && R16(o) != 0) {
        W16(o, R16(o) | 0x8000);
        W8(o + 0x18, R8(o + 0x18) | 1);
        ll::waitVBlank();
    }
    W16(o + 6, dest);
    if (dest == here && !quiet) W8(o + 0x18, R8(o + 0x18) | 1);
    bool refresh = false;
    if (from == 1) {
        removeFromInventory(u16(number - 3));
        refresh = dest != 1;
    }
    if (!refresh) {
        if (dest != 1) return done();
        u32 p = 0xFF0A1E;
        u16 count = 0;
        while (RS16(p) >= 0) { p += 2; count++; }
        W16(p, u16(number - 3));
        W16(p + 2, 0xFFFF);
        count >>= 2;
        if (u16(R16(0xFF06EC) + 1) != count) W16(0xFF06EC, count);
    }
    if (!BTST(0xFF09DE, 2) && !quiet) {
        ll::waitVBlank();
        ui::redrawInventory();
        ui::updatePageArrows();
    }
    done();
}

// ---- actors ------------------------------------------------------------

void opHideActor(bool exec) {   // 0x16
    if (exec) bsetReg(0xFF0ABF, actorBit(arg(2)));
    pc += 4;
}

void opShowActor(bool exec) {   // 0x19
    if (exec) {
        const int bit = actorBit(arg(2));
        bsetReg(0xFF0ABE, bit);
        actor::spin([bit] { return btstReg(0xFF0ABD, bit); });
        bclrReg(0xFF0ABF, bit);
    }
    pc += 4;
}

void opPlayAnim(bool exec) {   // 0x06
    if (exec) {
        const u16 who = arg(2), anim = arg(4);
        if (who == 1 || who == 2) {
            const int bit = who - 1;
            W16(0xFF0488 + bit * 2, anim);
            BSET(0xFF0AB5, bit);
            actor::spin([bit] { return !BTST(0xFF0AB5, bit); });
        } else {
            const u32 o = objectAddr(who);
            const u8 slot = R8(o + 0x19);
            if (!(slot & 0x80)) {
                W16(0xFF048C + slot * 2, anim);
                W16(0xFF061C + slot * 2, 0xFFFF);
                const int bit = slot + 2;
                bsetReg(0xFF0AB5, bit);
                actor::spin([bit] { return btstReg(0xFF0ABD, bit); });
            }
            // 0x2BEC stores the animation number at the start of the object
            // table, with no index: that is object 3's first word, which the
            // scripts use as a scratch variable. It is not the object's own state.
            W16(kObjects, anim);
        }
    }
    pc += 6;
}

void opStopOrFlag(bool exec) {   // 0x20
    if (exec) {
        const int bit = actorBit(arg(2));
        if (btstReg(0xFF0AB7, bit)) bclrReg(0xFF0AB7, bit);
        else bsetReg(0xFF0AC3, bit);
    }
    pc += 4;
}

void opActorRoutine(bool exec) {   // 0x21
    if (exec) {
        const u16 who = arg(4);
        s16 idx;
        if (who == 1) idx = 0;
        else if (who == 2) idx = 1;
        else idx = s16(RS8(objectAddr(who) + 0x19) + 2);
        W32(0xFF05F4 + idx * 4, pc + 8);
        W8(0xFF0AA5 + idx, u8(arg(6)));
        W8(0xFF0AAB + idx, u8(arg(6)));
        W16(0xFF060C + idx * 2, 0);
        bclrReg(0xFF0AC3, idx);
        bsetReg(0xFF0AC0, idx);
    }
    pc += 8 + sarg(2);
}

void opWaitWalk(bool exec) {   // 0x1F
    if (exec) {
        const int bit = actorBit(arg(2));
        actor::spin([bit] { return !btstReg(0xFF0AB7, bit) && !btstReg(0xFF0AC0, bit); });
    }
    pc += 4;
}

void opWaitAnim(bool exec) {   // 0x17
    if (exec) {
        const int bit = actorBit(arg(2));
        actor::spin([bit] { return btstReg(0xFF0ABC, bit); });
    }
    pc += 4;
}

// 0x0E (to a numbered room point) and 0x1B (to explicit coordinates).
void opMoveActor(bool exec) {
    const bool byPoint = arg(0) == 0x0E;
    if (exec) {
        const u16 saved = verb;
        const u16 who = arg(2) & 0x7FFF;
        if (who != 0) {
            u16 x, y;
            if (byPoint) {
                const u32 pt = R32(0xFF0696) + s16(u16(arg(8) << 2));
                x = u16(R16(pt) << 3);
                y = u16(R16(pt + 2) << 3);
            } else {
                x = arg(8);
                y = arg(0x0A);
            }
            const s16 mode = sarg(4);
            const u16 anim = arg(6);
            if (who == 1 || who == 2) {
                const int i = who - 1;
                if (mode < 0) {
                    W16(0xFF04DC + i * 4, x);
                    W16(0xFF04F4 + i * 4, y);
                    if (s16(anim) >= 0) {
                        W16(0xFF0488 + i * 2, anim);
                        W16(0xFF0618 + i * 2, i == 0 ? 0xFFFF : 1);
                        BSET(0xFF0AB5, i);
                        actor::spin([i] { return BTST(0xFF0ABD, i); });
                    }
                } else if (i == 0) {
                    actor::walkLeadTo(x, y, u16(mode), anim);
                } else {
                    actor::walkCompanionTo(x, y, u16(mode), anim);
                }
            } else {
                actor::moveObjectActor(who, x, y, mode, anim);
            }
        }
        verb = saved;
    }
    pc += byPoint ? 10 : 12;
}

void opWalkToObject(bool exec) {   // 0x0F
    const auto done = [] { pc += 12; };
    if (!exec) return done();
    const u16 saved = verb;
    const u16 who = arg(2) & 0x7FFF;
    u32 base = 0;
    if (who == 1) base = R32(0xFF0470);
    else if (who == 2) base = R32(0xFF0474);
    else {
        const u16 id = R16(objectAddr(who) + 2);
        for (int i = 0; i < 0x24 && !base; i++)
            if (R16(R32(0x32670 + i * 4)) == id) base = R32(0x32670 + i * 4);
    }
    if (base) {
        u16 x = arg(8), y;
        const u16 target = arg(4);
        if (target == 1) {
            x = u16(x + R16(0xFF04DC));
            y = R16(0xFF04F4);
        } else if (target == 2) {
            x = u16(x + R16(0xFF04E0));
            y = R16(0xFF04F8);
        } else {
            const u16 idx = u16(u32(u16(target - 3)) * 0x1A);
            const u32 o = kObjects + s16(idx);
            u16 standY = 0;
            x = actor::standOffset(base, u16(x + R16(o + 8)), &standY);
            if (BTST(o + 0x18, 1)) {
                x = u16(x * 2 + R16(o + 0x12));
                y = R16(o + 0x14);
            } else {
                const u32 hdr = R32(RoomHeaderPtr);
                const u32 rect = R32(R32(hdr + 0x24) + s16(u16((R16(o + 2) - 1) << 2))) + R32(hdr + 0x1C);
                x = u16(x + R16(rect + 8));
                y = u16(standY + R16(rect + 0x0A));
            }
        }
        const u16 anim = arg(6);
        if (who == 1) {
            actor::walkLeadTo(x, y, arg(0x0A), anim);
            W16(0xFF05B8, R16(objectAddr(arg(4)) + 8));
        } else if (who == 2) {
            // Deviation from the original: 0x5AD8 does not clear the companion's
            // "settling after a wander" state (bit 5 of 0xFF09DE) the way the
            // other walk commands do. If a script walk starts in that state he
            // waits forever on a looping animation and the game hangs.
            BCLR(0xFF09DE, 5);
            actor::companionWalkStart(x, y, arg(0x0A), anim);   // 0x5AD8
            W16(0xFF05BA, R16(objectAddr(arg(4)) + 8));
        } else {
            actor::moveObjectActor(who, x, y, sarg(0x0A), anim);
        }
    }
    verb = saved;
    done();
}

// ---- speech, sound, time, rooms -----------------------------------------

void faceLead(u16 facing) {   // 0x4DB0-0x4E0A, also 0x4E7A-0x4ED4
    const u16 idx = u16((R16(0xFF05B8) << 3) + facing * 2);
    W16(0xFF05B8, facing);
    W16(0xFF0488, R16(0xFC740 + idx));
    BSET(0xFF0AB5, 0);
    actor::spin([] { return !BTST(0xFF0ABC, 0); });
    actor::spin([] { return BTST(0xFF0ABC, 0); });
    W16(0xFF0488, u16(R16(0xFF05B8) + 4));
    BSET(0xFF0AB5, 0);
}

void opSay(bool exec) {   // 0x10
    if (exec) {
        const u16 saved = verb;
        const u16 flags = arg(2);
        if (flags & 0x1000) {
            BSET(0xFF0ABC, 0);
            BSET(0xFF09DE, 4);
        }
        u16 oldFacing = 0;
        if (flags & 0x2000) {
            oldFacing = R16(0xFF05B8);
            faceLead(flags >> 14);
        }
        ui::clearTopText();
        const u16 attr = R16(TextAttrBase);
        const u16 speaker = u16((attr & 0x9FFF) | ((flags & 0x30) << 9));
        W16(TextAttrBase, speaker);
        ll::loadFont(flags & 0x0F, 1, speaker);
        ui::showMessage(R32(R32(RoomHeaderPtr) + 0x20) + R32(pc + 4));
        BCLR(0xFF09DE, 4);
        W16(TextAttrBase, attr);
        ll::loadFont(0x0F, 1, attr);
        if (flags & 0x2000) faceLead(oldFacing);
        verb = saved;
    }
    pc += 8;
}

void opSound(bool exec) {   // 0x0D
    if (exec) {
        if (sarg(2) < 0) snd::stopSequence(arg(2) & 0x7FFF);
        else snd::startSequence(arg(2));
    }
    pc += 4;
}

void opWait(bool exec) {   // 0x15
    if (exec) {
        W16(0xFF08AE, arg(2));
        actor::spin([] { return RS16(0xFF08AE) < 0; });
    }
    pc += 4;
}

void opChangeRoom(bool exec) {   // 0x18
    if (exec) {
        W16(0xFF06AC, arg(2));
        W16(0xFF05B8, arg(6));
        W16(0xFF069E, u16(arg(4) << 2));
        room::enter();
    }
    pc += 8;
}

void opChangeRoomFade(bool exec) {   // 0x11
    if (exec) {
        W16(0xFF06AC, arg(2));
        W16(0xFF05B8, arg(6));
        const u8 flags = R8(EngineFlags);
        BCLR(EngineFlags, 6);
        ll::fadeOut();
        W8(EngineFlags, flags);
        W16(0xFF069E, u16(arg(4) << 2));
        room::enterAndShow();
    }
    pc += 8;
}

void opNop(bool) { pc += 2; }   // 0x00

}  // namespace
void opNative(bool exec);       // 0x0C, defined in natives.cpp
namespace {

using Handler = void (*)(bool);
const Handler kHandlers[0x22] = {
    opNop,            opVerbHeader,     opChoiceHeader,  opIf,             // 00-03
    opIfBlock,        opMoveObject,     opPlayAnim,      opRedrawObject,   // 04-07
    opAssign,         opSetObjectByte,  opSetObjectIcon, opSetObjectState, // 08-0B
    opNative,         opSound,          opMoveActor,     opWalkToObject,   // 0C-0F
    opSay,            opChangeRoomFade, opSetObjectHotspot, opTickHeader,  // 10-13
    opSetObjectHotspot, opWait,         opHideActor,     opWaitAnim,       // 14-17
    opChangeRoom,     opShowActor,      opPaletteCycle,  opMoveActor,      // 18-1B
    opCopyWalking,    opCopyAnimDone,   opFace,          opWaitWalk,       // 1C-1F
    opStopOrFlag,     opActorRoutine,                                      // 20-21
};

void dispatch(bool exec) {
    const u16 op = R16(pc);
    if (trace) trace(pc, op, exec);
    if (op >= 0x22) {
        std::fprintf(stderr, "script: bad opcode %04X at %06X\n", op, pc);
        pc = end;
        return;
    }
    kHandlers[op](exec);
}

}  // namespace

int actorBit(u16 who) {
    if (who == 1) return 0;
    if (who == 2) return 1;
    return (R8(objectAddr(who) + 0x19) + 2) & 7;
}

u32 objectAddr(u16 number) {
    return kObjects + s16(u16(u32(u16(number - 3)) * 0x1A));
}

void runScan() {
    while (s32(end) > s32(pc)) {
        dispatch(false);
        if (BTST(kVmFlags, 0)) return;
    }
}

void runExec() {
    for (;;) {
        if (BTST(0xFF0ACA, 1) && BTST(0xFF0ACA, 0)) return;   // sequence skipped with Start
        if (s32(end) <= s32(pc)) return;
        dispatch(true);
        if (BTST(kVmFlags, 0)) return;
        if (s32(pc) >= s32(end)) return;
    }
}

void runActive() {
    if (R32(0xFF0878) == ScanLoop) runScan();
    else runExec();
}

}  // namespace vm
