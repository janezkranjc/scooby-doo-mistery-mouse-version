#include <cstdlib>
#include <cstdio>
// Native functions reachable from scripts through opcode 0x0C (table 0x2EB4).
#include "actors.h"
#include "addr.h"
#include "autoplay.h"
#include "lowlevel.h"
#include "menu.h"
#include "room.h"
#include "script.h"
#include "sound.h"
#include "ui.h"
#include <cstdio>

namespace vm {

namespace {

void minigameArena();   // 0x3142, not yet ported

void setShake() {   // 0x4274
    W16(0xFF08AA, 0xFFFF);
    W16(0xFF08AC, 1);
    W8(0xFF0ACF, R8(0x3247C));
    BSET(0xFF09E8, 7);
}

void markerLarge() {   // 0x42A2
    W16(0xFF08A4, 0x07F8);
    ll::vramWrite(0xFF00, 0x80, 0x321BE);
    W32(0xFF08A6, 0x323BE);
    W8(0xFF0ACE, 7);
    BSET(0xFF09E8, 5);
    BSET(0xFF09E8, 4);
}

void markerSmall() {   // 0x42E0
    W16(0xFF08A4, 0x47F8);
    ll::vramWrite(0xFF00, 0x40, 0x322BE);
    ll::vramWrite(0xFF80, 0x40, 0x322BE);
    W32(0xFF08A6, 0x323BE);
    BSET(0xFF09E8, 5);
    BSET(0xFF09E8, 4);
}

void attachPartner() {   // 0x432C
    actor::spin([] { return BTST(0xFF0ABC, 0); });
    u16 slot = 2;
    while (BTST(0xFF0AB4, slot) && ++slot != 6) {}
    W16(0xFF08B0, slot);
    BSET(0xFF09E8, 3);
    W8(0xFF0AC6, 0);
    ll::waitVBlank();
}

void randomSound(u16 base) {   // 0x4374 / 0x4390
    const u16 r = ll::random(2);
    snd::startSequence(r ? u16(r + base + 1) : base);
}

void panCamera(bool right) {   // 0x43C0 / 0x4428
    BSET(0xFF09E8, 1);
    BCLR(CameraX + 1, 0);
    if (right) {
        const u16 x = u16((R16(CameraX) + 8) & 0xFFF8);
        const u16 limit = u16((R16(0xFF06C6) - 0x20) << 3);
        if (s16(x) >= s16(limit)) return;
        W16(0xFF08B4, BTST(0xFF09E9, 4) ? 8 : 2);
        W16(0xFF08B6, limit);
    } else {
        if ((R16(CameraX) & 0xFFFE) == 0) return;
        W16(0xFF08B6, 0);
        W16(0xFF08B4, BTST(0xFF09E9, 4) ? 0xFFF8 : 0xFFFE);
    }
    BSET(0xFF09E9, 5);
    actor::spin([] { return !BTST(0xFF09E9, 5); });
}

void overlayOn() {   // 0x44BA: plane A becomes a mask that follows the lead
    while (R8(0xFF0AB6)) ll::idleFrame();
    BSET(0xFF09DE, 6);
    ll::lzssDecode(0x31DFA, 0xFF3000, 0xFF4000);
    const u16 base = R16(0xFF068A);
    ll::dmaVram(0xFF4000, u16((base & 0x7FF) << 5), u16(R32(0xFF3000)) >> 1);
    ll::vramFill(0xC000, 0x800, u16(base + 1));
    u32 src = 0x320CC;
    u16 addr = 0xC000;
    for (int r = 0; r <= 10; r++) {
        for (int c = 0; c <= 10; c++) {
            ll::vramPoke(addr, u16(R16(src) + base));
            src += 2;
            addr = u16(addr + 2);
        }
        addr = u16(addr + 0x6A);
    }
}

void waitForButton() {   // 0x4568
    for (;;) {
        if (autoplay::active) { actor::update(); ll::idleFrame(); return; }
        actor::objectTicks();
        actor::update();
        if (!BTST(PadState, 6)) { if (BTST(PadPrev, 6)) return; }
        else if (!BTST(PadState, 4) && BTST(PadPrev, 4)) return;
        ll::idleFrame();
    }
}

// 0x45A4. Shows a full-screen picture over the game with a colour cycle
// until a button is pressed, then restores the screen and opens the text box.
void closeUp() {
    if (BTST(0xFF2A00, 1)) return;
    BSET(0xFF2A00, 1);
    if (R16(pc + 2) != 0x0C) {
        while (R8(0xFF0AB6)) ll::idleFrame();
        BCLR(EngineFlags, 6);
        ll::waitVBlank();
        ll::cramClear(0, 0x80);
        for (int i = 0; i < 0x2200; i++) W16(0xFF2C00 + 2 * i, M.vdp.vramWord(u16(2 * i)));
        M.vdp.regWord(0x9200);
        W8(0xFF0AD1, 0);
        ll::vramWrite(0, 0x1E1F, 0x2B33A);
        const u16 camX = R16(CameraX), camY = R16(CameraY);
        W16(CameraX, 0);
        W16(CameraY, 0);
        ll::vramClear(0xC000, 0x800);
        ll::vramClear(0xE000, 0x800);
        ll::vramWrite(0xC080, 0x4C0, 0x2EF7A);
        u16 addr = 0xE080;
        for (int r = 0; r <= 0x12; r++) {
            ll::vramWrite(addr, 0x20, 0x2F97A);
            addr = u16(addr + 0x40);
            ll::vramWrite(addr, 0x20, 0x2F97A);
            addr = u16(addr + 0x40);
        }
        for (int i = 0; i < 0x40; i++) W16(0xFF08BC + 2 * i, R16(0x2F8FA + 2 * i));
        ll::vramPoke(0xD800, 0);
        ll::vramPoke(0xD802, 0);
        s16 hold = 3;
        for (int n = 0xC8; n >= 0; n--) {
            if (!BTST(PadState, 6) || !BTST(PadState, 4)) break;
            if (--hold < 0) {
                hold = 3;
                BCHG(CameraX, 0);
            }
            ll::waitVBlank();
            ll::dmaCram(0xFF08BC, 0, 0x40);
            const u16 first = R16(0xFF090C);
            for (int i = 0; i < 7; i++) W16(0xFF090C + 2 * i, R16(0xFF090E + 2 * i));
            W16(0xFF091A, first);
        }
        ll::cramClear(0, 0x40);
        W16(CameraY, camY);
        W16(CameraX, camX);
        M.vdp.regWord(0x9202);
        W8(0xFF0AD1, 2);
        ll::vramWrite(0, 0x2200, 0xFF2C00);
        room::restorePanel();
        BSET(EngineFlags, 6);
        actor::update();
        BSET(0xFF09DE, 1);
        ui::refreshStatusLine();
        ll::waitFrames(5);
        BCLR(EngineFlags, 6);
        ll::waitVBlank();
        ll::dmaCram(TargetPalette, 0, 0x40);
        BSET(EngineFlags, 6);
    }
    ui::openTextBox();
}

void leadSpecial(u16 anim, u16 x, u16 y, u16 partnerA, u16 partnerB) {   // 0x301E / 0x30A6
    W16(0xFF0488, anim);
    while (BTST(0xFF0AB6, 0)) ll::idleFrame();
    BSET(0xFF0AB5, 0);
    actor::update();
    W16(0xFF04DC, x);
    W16(0xFF04F4, y);
    actor::spin([] { return BTST(0xFF0ABC, 0) && (BTST(0xFF09DE, 7) || !BTST(0xFF0AC0, 1)); });
    W16(0xFF048C, BTST(0xFF09DE, 7) ? partnerB : partnerA);
    BSET(0xFF0AB5, 2);
    actor::update();
    W8(0xFF0ABF, R8(0xFF0ABF) | 3);
}

void throwGame() {   // 0x3F58
    BSET(0xFF09E9, 2);
    s16 dx = 0;
    W8(PadState, 0xFF);
    if (!BTST(0xFF0AC0, 2)) W16(0xFF0610, 0);
    for (;;) {
        W16(0xFF08AE, 0);
        bool fire = false;
        for (;;) {
            actor::update();
            if (!BTST(PadState, 2)) dx = -2;
            if (!BTST(PadState, 3)) dx = 2;
            const s16 x = s16(dx + RS16(0xFF04E8));
            if (x >= 0x64 && x <= 0x94) { W16(0xFF04E8, u16(x)); W16(0xFF04EC, u16(x)); }
            if (autoplay::active) {
                // The solver fires when the target will be in the hit window
                // once the claw has come down.
                // Measured: the target advances about 60 while the claw takes its
                // 37 frames to come down.
                static int passes = 0;
                if (++passes > 4000) { passes = 0; fire = true; break; }   // target never came round: give up the try
                const s16 now = RS16(0xFF0610);
                const s16 tt = s16(now + 60);
                if (R16(0xFF04E8) == 0x94 ? (tt >= 0x62 && tt <= 0x72) : (tt >= 0xFE && tt <= 0x10E)) { passes = 0; fire = true; break; }
            }
            if (!BTST(PadState, 6) || !BTST(PadState, 4)) { fire = true; break; }
            if (RS16(0xFF08AE) < 0) break;
            ll::idleFrame();
        }
        if (fire) break;
    }
    for (u16 y = R16(0xFF0500);;) {
        W16(0xFF0500, y);
        ll::waitVBlank();
        y = u16(y + 2);
        if (s16(y) >= 0x4A) break;
    }
    W16(0xFF0500, 0x4A);
    const s16 t = RS16(0xFF0610);
    const bool hit = R16(0xFF04E8) == 0x94 ? (t >= 0x5C && t <= 0x78) : (t >= 0xF8 && t <= 0x114);
    if (hit) snd::startSequence(0x63);
    W16(0xFF048E, hit ? 0x1C : 0x1B);
    W16(0xFF120C, hit ? 1 : 0);
    BSET(0xFF0AB5, 3);
    actor::spin([] { return BTST(0xFF0ABC, 3); });
    snd::startSequence(0x62);
    if (R16(0xFF120C)) BSET(0xFF0ABF, 2);
    do {
        ll::waitVBlank();
        W16(0xFF0500, u16(R16(0xFF0500) - 1));
    } while (R16(0xFF0500) != 0);
    BCLR(0xFF09E9, 2);
    BCLR(EngineFlags, 6);
    ll::fadeOut();
    BSET(EngineFlags, 6);
    BCLR(0xFF0ABF, 2);
}

void pickDirection() {   // 0x415A
    for (;;) {
        actor::update();
        if (autoplay::active) { W16(0xFF120C, u16(autoplay::chooseLine(3))); break; }   // the solver's plan picks the way
        if (!BTST(PadState, 2)) { W16(0xFF120C, 0); break; }
        if (!BTST(PadState, 3)) { W16(0xFF120C, 1); break; }
        if (!BTST(PadState, 1)) { W16(0xFF120C, 2); break; }
        ll::idleFrame();
    }
    BSET(0xFF0AC3, 0);
}

void credits() {   // 0x41B6
    ll::waitFrames(0x1E);
    BSET(0xFF09EB, 1);
    ll::lzssDecode(0x296FA, 0xFF3000, 0xFF7000);
    ll::vramWrite(u16(R16(0xFF068A) << 5), u16(R32(0xFF3000)) >> 1, 0xFF7000);
    ll::lzssDecode(0x29C72, 0xFF3000, 0xFF7000);
    const u32 size = R32(0xFF3000);
    const u32 stop = 0xFF7000 + size;
    for (u32 i = 0; i < (u16(size) >> 1); i++) W16(0xFF7000 + 2 * i, u16(R16(0xFF7000 + 2 * i) + R16(0xFF068A)));
    u32 src = 0xFF7000;
    u16 row = 0x1C, scroll = 0;
    bool odd = false;
    do {
        for (;;) {
            W16(ScrollAY, scroll);
            actor::update();
            ll::waitVBlank();
            autoplay::idleCalls++;   // the credits are long; tell the solver's stall check we are alive
            odd = !odd;
            if (odd) continue;
            if ((++scroll & 7) == 0) break;
        }
        row = (row + 1) & 0x1F;
        src = ll::vramWrite(u16((row << 7) + 0xC000), 0x20, src);
    } while (s32(stop) > s32(src));
    W16(0xFF08AE, 0x12C);
    actor::spin([] { return RS16(0xFF08AE) < 0; });
    BCLR(EngineFlags, 6);
    ll::fadeOut();
    BCLR(EngineFlags, 6);
    ll::fadeOut();
    throw menu::Restart{};
}

void callNative(u16 fn) {
    switch (fn) {
        case 0x00: break;
        case 0x01: BSET(0xFF0AC9, 0); break;
        case 0x02: room::show(); break;
        case 0x03: { const u8 f = R8(EngineFlags); BCLR(EngineFlags, 6); ll::fadeOut(); W8(EngineFlags, f); break; }
        case 0x04: waitForButton(); break;
        case 0x05: case 0x0C: closeUp(); break;
        case 0x06: if (BTST(0xFF2A00, 1)) ui::closeTextBox(); break;
        case 0x07: overlayOn(); break;
        case 0x08: BCLR(0xFF09DE, 6); break;
        case 0x09: BSET(0xFF09DE, 7); BSET(0xFF0ABF, 1); break;
        case 0x0A: BCLR(0xFF09DE, 7); BCLR(0xFF0ABF, 1); break;
        case 0x0B: W16(0xFF048A, u16(R16(0xFF05BA) + 4)); BSET(0xFF09EB, 2); break;
        case 0x0D: BSET(0xFF09E8, 1); break;
        case 0x0E: BCLR(0xFF09E8, 1); break;
        case 0x0F: panCamera(true); break;
        case 0x10: panCamera(false); break;
        case 0x11: BSET(0xFF09E8, 2); break;
        case 0x12: BCLR(0xFF09E8, 2); break;
        case 0x13: randomSound(0x25); break;
        case 0x14: randomSound(0x28); break;
        case 0x15: attachPartner(); break;
        case 0x16: BCLR(0xFF09E8, 3); break;
        case 0x17: markerLarge(); break;
        case 0x18: markerSmall(); break;
        case 0x19: BCLR(0xFF09E8, 4); break;
        case 0x1A: setShake(); break;
        case 0x1B: BCLR(0xFF09E8, 7); break;
        case 0x1C: ui::closeTextBox(); break;
        case 0x1D: ui::openTextBox(); break;
        case 0x1E: credits(); break;
        case 0x1F: BSET(0xFF09E9, 0); break;
        case 0x20: BCLR(0xFF09E9, 0); break;
        case 0x21: pickDirection(); break;
        case 0x22:
            W16(0xFF0488, RS16(0xFF04DC) >= 0x114 ? 0x4A : 0x49);
            W16(0xFF0618, 0xFFFF);
            BSET(0xFF0AB5, 0);
            actor::spin([] { return BTST(0xFF0ABD, 0); });
            break;
        case 0x23: BSET(0xFF09E9, 1); break;
        case 0x24: BCLR(0xFF09E9, 1); break;
        case 0x25: actor::spin([] { return !BTST(0xFF0AC0, 0); }); break;
        case 0x26: W16(0xFF120C, BTST(0xFF0AC0, 0) ? 0 : 1); break;
        case 0x27: throwGame(); break;
        case 0x28: BSET(0xFF0ACA, 2); break;
        case 0x29: BSET(0xFF09E9, 3); break;
        case 0x2A: BCLR(0xFF09E9, 3); break;
        case 0x2B:
            BSET(0xFF09E9, 4);
            BCLR(0xFF0AB8, 0);
            W32(0xFF0470, 0xB5D02);
            W16(0xFF0488, 0x1E);
            BSET(0xFF0AB5, 0);
            actor::spin([] { return BTST(0xFF0ABD, 0); });
            BCLR(EngineFlags, 7);
            break;
        case 0x2C: BSET(0xFF0AB8, 0); W32(0xFF0470, 0x32700); BCLR(0xFF09E9, 4); BCLR(EngineFlags, 7); break;
        case 0x2D: BSET(0xFF09E9, 6); break;
        case 0x2E: BCLR(0xFF09E9, 6); break;
        case 0x2F: BSET(0xFF0ACA, 3); W16(0xFF06DA, R16(0xFF120C)); break;
        case 0x30: BSET(0xFF09E9, 7); break;
        case 0x31: BCLR(0xFF09E9, 7); break;
        case 0x32: minigameArena(); break;
        case 0x33: BSET(0xFF09EB, 0); break;
        case 0x34: BCLR(0xFF09EB, 0); break;
        case 0x35: leadSpecial(0x45, 0x128, 0x40, 0, 4); break;
        case 0x36: BCLR(0xFF09EB, 2); break;
        case 0x37: leadSpecial(0x47, 0x58, 0x50, 1, 5); break;
        case 0x38: BCLR(0xFF0ACA, 1); break;
        case 0x39: BSET(0xFF0ACA, 1); if (BTST(0xFF09ED, 2)) BSET(0xFF0ACA, 0); break;
        case 0x3A: BSET(0xFF09EB, 5); break;
        case 0x3B: BCLR(0xFF09EB, 5); break;
        case 0x3C: BSET(0xFF09EB, 6); break;
        case 0x3D: BCLR(0xFF09EB, 6); break;
        default: std::fprintf(stderr, "script: bad native %02X at %06X\n", fn, pc); break;
    }
}

// ---- arena mini-game (0x3142-0x3D8D) -------------------------------------
// Two vehicles with eight headings. The player steers with left and right and
// accelerates with A or B. Crashes cost energy according to who hit whom.

namespace arena {

constexpr u32 kMotion = 0x3D96;     // per heading: eight X steps then eight Y steps, by speed
u16 opposite(u16 heading) { return R16(0x3D86 + heading * 2); }

void drawBar(u32 buf, s16 energy, u32 colour) {   // 0x393A: eight tiles, one unit per pixel column
    for (int tile = 0; tile < 8; tile++) {
        u32 row = 0, bit = colour;
        for (int k = 0; k < 8; k++) {
            if (energy >= 0) { row |= bit; bit >>= 4; energy--; }
        }
        const u32 rows[8] = {0, 0, row, row, row, row, 0, 0};
        for (u32 v : rows) { W32(buf, v); buf += 4; }
    }
}
void drawBars() {
    drawBar(0xFF08BC, RS16(0xFF0A98), 0xA0000000);
    drawBar(0xFF0328, RS16(0xFF0A9A), 0x90000000);
}
void uploadBars() {
    ll::vramWrite(0xA300, 0x80, 0xFF08BC);
    ll::vramWrite(0xA400, 0x80, 0xFF0328);
}

bool coinFlip() {   // the routine's own generator step (0x3BBE)
    u32 v = ~R32(0xFF0428);
    v = v << 1 | v >> 31;
    v = v << 16 | v >> 16;
    W32(0xFF0428, v);
    return v & 0x8000;
}

// 0x3A7A: heading from the player to the opponent, 0..7.
u16 bearing() {
    s16 dx = s16(R16(0xFF04E4) - R16(0xFF04DC)), dy = s16(R16(0xFF04FC) - R16(0xFF04F4));
    u16 q = 0;
    if (dx < 0) { dx = s16(-dx); q = 2; }
    if (dy < 0) { dy = s16(-dy); q++; }
    if (!(q & 2)) q ^= 1;
    q = u16(q * 2);
    s16 a = dx, b = dy, c = dx, d = dy;
    if (q & 2) { const s16 t = a; a = b; b = t; }
    if (!(a < b)) q++;
    const u16 sel = (q + 1) & 2;
    q = u16(q * 2);
    if (sel == 0) c = s16(c * 2); else d = s16(d * 2);
    if (q & 4) { const s16 t = c; c = d; d = t; }
    if (!(c < d)) q++;
    return ((q + 1) & 0x0F) >> 1;
}

// 0x39D4: do the two vehicles' boxes overlap?
bool overlap() {
    auto box = [](u32 base, u16 anim, u16 x, u16 y, s16 v[4]) {
        const u32 f = base + s16(R16(base + s16(u16(anim * 2 + R16(base + 4))))) + 0x0E;
        v[0] = s16(x - R16(f)); v[1] = s16(y - R16(f + 2)); v[2] = s16(x - R16(f + 4)); v[3] = s16(y - R16(f + 6));
    };
    s16 p[4], o[4];
    box(0xF88DC, R16(0xFF0488), R16(0xFF04DC), R16(0xFF04F4), p);
    box(0xFA77A, R16(0xFF048C), R16(0xFF04E4), R16(0xFF04FC), o);
    if (p[0] > o[0]) { if (o[2] < p[0]) return false; } else if (!(p[2] >= o[0])) return false;
    if (p[1] > o[1]) { if (o[3] < p[1]) return false; } else if (!(p[3] >= o[1])) return false;
    return true;
}

void crash() {   // 0x3AF0
    W8(0xFF09EA, R8(0xFF09EA) | 0xC0);
    snd::startSequence(0x72);
    u16 toward = bearing();
    const u16 away = opposite(toward);
    u16 dmgOpp = R16(0xFF0A90);
    if (toward != R16(0xFF0488)) dmgOpp >>= 1;
    if (away == R16(0xFF048C)) dmgOpp = 0;
    else if (toward == R16(0xFF048C)) dmgOpp >>= 1;
    W16(0xFF0A9A, u16(R16(0xFF0A9A) - dmgOpp));
    u16 dmgMe = R16(0xFF0A92);
    if (away != R16(0xFF048C)) dmgMe >>= 1;
    if (toward == R16(0xFF0488)) dmgMe = 0;
    else if (away == R16(0xFF0488)) dmgMe >>= 1;
    W16(0xFF0A98, u16(R16(0xFF0A98) - dmgMe));
    if (dmgOpp != dmgMe) snd::startSequence(s16(dmgOpp) > s16(dmgMe) ? 0x25 : 0x28);
    W16(0xFF0A96, RS16(0xFF0A90) < 4 ? 4 : R16(0xFF0A90));
    W16(0xFF0A92, 0);
    W8(0xFF0AA1, 8); W8(0xFF0AA3, 8);
    BSET(0xFF09EA, coinFlip() ? 4 : 3);
    W16(0xFF05BC, toward);
    toward = opposite(toward);
    W16(0xFF0A94, RS16(0xFF0A92) < 4 ? 4 : R16(0xFF0A92));
    W16(0xFF0A90, 0);
    W8(0xFF0AA0, 8); W8(0xFF0AA2, 8);
    BSET(0xFF09EA, coinFlip() ? 2 : 1);
    W16(0xFF05B8, toward);
}

void bouncePlayer() {   // 0x3C70
    snd::startSequence(0x60);
    if (BTST(0xFF09EA, 6)) { W16(0xFF0A98, u16(R16(0xFF0A98) - R16(0xFF0A94))); BCLR(0xFF09EA, 6); }
    W16(0xFF0A94, R16(0xFF0A90));
    W16(0xFF0A90, 0);
    W8(0xFF0AA0, 8); W8(0xFF0AA2, 8);
    BSET(0xFF09EA, coinFlip() ? 2 : 1);
    W16(0xFF05B8, opposite(R16(0xFF0488)));
}

void bounceOpponent() {   // 0x3CFE
    snd::startSequence(0x60);
    if (BTST(0xFF09EA, 7)) { W16(0xFF0A9A, u16(R16(0xFF0A9A) - R16(0xFF0A96))); BCLR(0xFF09EA, 7); }
    W16(0xFF0A96, R16(0xFF0A92));
    W16(0xFF0A92, 0);
    W8(0xFF0AA1, 8); W8(0xFF0AA3, 8);
    BSET(0xFF09EA, coinFlip() ? 4 : 3);
    W16(0xFF05BC, opposite(R16(0xFF048C)));
}

// Applies own speed plus the push from the last impact. Returns true if the
// new position is blocked.
bool moveVehicle(u32 px, u32 py, u16 speed, u16 heading, u16 push, u16 pushHeading) {
    const u32 a = kMotion + s16(u16(speed * 2 + (heading << 5)));
    const u32 b = kMotion + s16(u16(push * 2 + (pushHeading << 5)));
    const u16 x = u16(R16(px) + R16(a) + R16(b)), y = u16(R16(py) + R16(a + 0x10) + R16(b + 0x10));
    W16(px, x); W16(py, y);
    W16(0xFF064A, x); W16(0xFF064E, y);
    return room::blockedProbe();
}

}  // namespace arena

void minigameArena() {
    using namespace arena;
    M.vdp.regWord(0x8A94);
    const u16 savedFacing = R16(0xFF05B8);
    {   // two tiles of frame art for the energy bars
        u32 p = 0xFF08BC;
        auto put = [&](u32 v) { W32(p, v); p += 4; };
        put(0xFF); for (int i = 0; i < 6; i++) put(0xF0); put(0xFF);
        put(0xFFFFFFFF); for (int i = 0; i < 6; i++) put(0); put(0xFFFFFFFF);
        ll::dmaVram(0xFF08BC, 0xFF00, 0x20);
        p = 0xFF08BC;
        for (int half = 0; half < 2; half++) {
            W16(p, 0x7F8); p += 2;
            for (int i = 0; i < 8; i++) { W16(p, 0x7F9); p += 2; }
            W16(p, 0xFF8); p += 2;
            W16(p, R16(0xFF08B8)); p += 2;
            W16(p, R16(0xFF08B8)); p += 2;
        }
    }
    const u16 inv = BTST(EngineFlags, 4) ? 0x40 : 0;
    ll::dmaVram(0xFF08BC, u16(0xAB0A + inv), 0x18);
    ll::dmaVram(0x1900C, 0x9380, 0x1C0);
    for (int side = 0; side < 2; side++) {   // the two portraits beside the bars
        u16 addr = u16(0xAB02 + inv + (side ? 0x36 : 0));
        u32 src = side ? 0x19394 : 0x1938C;
        for (int r = 0; r < 5; r++) {
            for (int c = 0; c < 4; c++, addr = u16(addr + 2), src += 2) ll::vramPoke(addr, u16(R16(src) + 0x49C));
            src += 8;
            addr = u16(addr + 0x78);
        }
    }
    W8(0xFF09EA, 0); BSET(0xFF09EA, 0);
    BSET(0xFF09E8, 2);
    BSET(0xFF09DE, 6);
    W16(0xFF05B8, 0);
    W32(0xFF0A90, 0); W32(0xFF0A94, 0); W32(0xFF0AA0, 0);
    W16(0xFF0A98, 0x3F); W16(0xFF0A9A, 0x3F);
    W8(0xFF0AA4, 0);
    W16(0xFF0A9C, 0xFFFF);
    drawBars();
    uploadBars();
    W32(0xFF0470, 0xF88DC);
    BCLR(0xFF0AB8, 0);
    W16(0xFF0488, 2);
    BSET(0xFF0AB5, 0);
    W16(0xFF0618, 0xFFFF);
    actor::spin([] { return BTST(0xFF0ABD, 0); });
    snd::stopSequence(0x58);
    snd::stopSequence(0x71);
    snd::startSequence(0x7B);
    ll::waitVBlank();
    room::show();
    snd::startSequence(0x79);
    ll::waitFrames(0x32);
    snd::startSequence(0x32);
    ll::waitFrames(0x64);

    int loserBit;
    for (int passes = 0;; passes++) {
        ll::waitVBlank();
        actor::update();
        if (autoplay::active && passes > 300) W16(0xFF0A9A, 0xFFFF);   // the solver does not drive; it takes the win branch
        if (RS16(0xFF0A98) < 0) { W16(0xFF0488, 8); BSET(0xFF0AB5, 0); W16(0xFF120C, 0); loserBit = 0; break; }
        if (RS16(0xFF0A9A) < 0) { W16(0xFF048C, 8); BSET(0xFF0AB5, 2); W16(0xFF120C, 1); loserBit = 2; break; }
        if (!BTST(PadState, 7) && BTST(PadPrev, 7)) {   // pause
            snd::pauseAll();
            snd::startSequence(0x59);
            do { ll::waitVBlank(); } while (!BTST(PadState, 7));
            do { ll::waitVBlank(); } while (BTST(PadState, 7));
            snd::startSequence(0x59);
            snd::resumeAll();
        }
        uploadBars();
        for (u32 t = 0xFF0AA0; t <= 0xFF0AA4; t++)
            if (R8(t)) W8(t, u8(R8(t) - 1));
        // pushes from impacts wear off
        if (R16(0xFF0A96) != 0) {
            if (R8(0xFF0AA3) == 0) { W8(0xFF0AA3, u8(7 - R16(0xFF0A96))); W16(0xFF0A96, u16(R16(0xFF0A96) - 1)); }
        } else {
            W8(0xFF09EA, R8(0xFF09EA) & 0xE7); BCLR(0xFF09EA, 7);
        }
        if (R16(0xFF0A94) != 0) {
            if (R8(0xFF0AA2) == 0) { W8(0xFF0AA2, u8(7 - R16(0xFF0A94))); W16(0xFF0A94, u16(R16(0xFF0A94) - 1)); }
        } else {
            W8(0xFF09EA, R8(0xFF09EA) & 0xF9); BCLR(0xFF09EA, 6);
        }
        // the opponent always accelerates
        if (R8(0xFF0AA1) == 0 && R16(0xFF0A92) != 7) {
            W16(0xFF0A92, u16(R16(0xFF0A92) + 1));
            W8(0xFF0AA1, u8(R16(0xFF0A92) + 8));
        }
        if (!BTST(PadState, 6) || !BTST(PadState, 4)) {
            if (R8(0xFF0AA0) == 0 && R16(0xFF0A90) != 7) {
                W16(0xFF0A90, u16(R16(0xFF0A90) + 1));
                W8(0xFF0AA0, u8(R16(0xFF0A90) + 8));
            }
        } else if (R16(0xFF0A90) != 0 && R8(0xFF0AA0) == 0) {
            W8(0xFF0AA0, u8(7 - R16(0xFF0A90)));
            W16(0xFF0A90, u16(R16(0xFF0A90) - 1));
        }
        // opponent steering: turn towards the player's far side
        if (!(R16(0xFF0A96) != 0 && R16(0xFF0A92) != 0) && BTST(0xFF0ABC, 2)) {
            u16 h = R16(0xFF048C);
            if (BTST(0xFF09EA, 3)) h++;
            if (BTST(0xFF09EA, 4)) h--;
            h &= 7;
            u16 want = opposite(bearing());
            if (want != h && R8(0xFF0AA4) == 0) {
                W8(0xFF0AA4, 0x14);
                if (want != R16(0xFF0A9C)) {
                    W16(0xFF0A9C, want);
                    const u16 diff = (want - h) & 7;
                    bool left;
                    if (s16(diff) <= 4 || BTST(0xFF09EA, 3)) left = BTST(0xFF09EA, 4);
                    else left = true;
                    W16(0xFF0A9E, left ? 0xFFFF : 1);
                }
                h = (h + R16(0xFF0A9E)) & 7;
            }
            if (h != R16(0xFF048C)) {
                W16(0xFF0A92, u16(R16(0xFF0A92) - 1));
                if (RS16(0xFF0A92) < 0) W16(0xFF0A92, 0);
                W16(0xFF048C, h);
                BSET(0xFF0AB5, 2);
            }
        }
        // player steering
        if ((R16(0xFF0A94) != 0 || R16(0xFF0A90) != 0) && BTST(0xFF0ABC, 0)) {
            u16 h = R16(0xFF0488);
            if (BTST(0xFF09EA, 1)) h++;
            if (!BTST(PadState, 3)) h++;
            if (BTST(0xFF09EA, 2)) h--;
            if (!BTST(PadState, 2)) h--;
            h &= 7;
            if (h != R16(0xFF0488)) {
                W16(0xFF0A90, u16(R16(0xFF0A90) - 1));
                if (RS16(0xFF0A90) < 0) W16(0xFF0A90, 0);
                W16(0xFF0488, h);
                BSET(0xFF0AB5, 0);
            }
        }
        W8(0xFF09EC, R8(0xFF09EC) & 0xFC);
        if (moveVehicle(0xFF04E4, 0xFF04FC, R16(0xFF0A92), R16(0xFF048C), R16(0xFF0A96), R16(0xFF05BC))) {
            bounceOpponent();
            BSET(0xFF09EC, 1);
        }
        if (moveVehicle(0xFF04DC, 0xFF04F4, R16(0xFF0A90), R16(0xFF0488), R16(0xFF0A94), R16(0xFF05B8))) {
            bouncePlayer();
            BSET(0xFF09EC, 0);
        }
        auto restorePlayer = [] { W16(0xFF04DC, R16(0xFF0A84)); W16(0xFF04F4, R16(0xFF0A86)); W16(0xFF0488, R16(0xFF0A8C)); W16(0xFF0618, 0xFFFF); };
        auto restoreOpponent = [] { W16(0xFF04E4, R16(0xFF0A88)); W16(0xFF04FC, R16(0xFF0A8A)); W16(0xFF048C, R16(0xFF0A8E)); W16(0xFF061C, 0xFFFF); };
        if (overlap()) {
            crash();
            restorePlayer();
            restoreOpponent();
            W8(0xFF0AB5, R8(0xFF0AB5) | 5);
        } else {
            if (BTST(0xFF09EC, 0)) { restorePlayer(); BSET(0xFF0AB5, 0); }
            if (BTST(0xFF09EC, 1)) { restoreOpponent(); BSET(0xFF0AB5, 2); }
        }
        W16(0xFF0A8E, R16(0xFF048C)); W16(0xFF0A8C, R16(0xFF0488));
        W16(0xFF0A84, R16(0xFF04DC)); W16(0xFF0A86, R16(0xFF04F4));
        W16(0xFF0A88, R16(0xFF04E4)); W16(0xFF0A8A, R16(0xFF04FC));
        drawBars();
    }
    actor::spin([loserBit] { return BTST(0xFF0ABC, loserBit); });
    { const u8 f = R8(EngineFlags); BCLR(EngineFlags, 6); ll::fadeOut(); W8(EngineFlags, f); }
    M.vdp.regWord(0x8AA3);
    snd::stopSequence(0x7B);
    snd::startSequence(0x71);
    W16(0xFF05B8, savedFacing);
    BCLR(0xFF09DE, 6);
    W32(0xFF0470, 0x32700);
    BSET(0xFF0AB8, 0);
    BCLR(0xFF09EA, 0);
    BCLR(0xFF09E8, 2);
}

}  // namespace

void opNative(bool exec) {
    if (exec) callNative(R16(pc + 2));
    pc += 4;
}

}  // namespace vm
