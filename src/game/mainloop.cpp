// Episode start-up and the adventure's main loop (0xCB4-0x13B6, 0x13BA,
// 0x19B0, 0x7F66).
#include "actors.h"
#include "addr.h"
#include "autoplay.h"
#include "game.h"
#include "interrupts.h"
#include "lowlevel.h"
#include "menu.h"
#include "room.h"
#include "script.h"
#include "sound.h"
#include "ui.h"

namespace game {

void openingAnimation();
void resetGameState();

namespace {

// 0x175C: six sprites laid out as one 80x48 actor frame.
void introSprite(u16& addr, s16& x, s16& y, u16& link, u16 tile) {
    for (int k = 0; k < 6; k++) {
        y = s16(y + RS16(0xFC8B4 + k * 2));
        ll::vramPoke(addr, u16(y));
        ll::vramPoke(u16(addr + 2), u16(link | R16(0xFC99C + k * 2)));
        ll::vramPoke(u16(addr + 4), u16(tile + R16(0xFC944 + k * 2)));
        link++;
        x = s16(x + RS16(0xFC89C + k * 2));
        ll::vramPoke(u16(addr + 6), x == 0 ? 0xFFFF : u16(x));
        addr = u16(addr + 8);
    }
}

void introSprites() {   // 0x16C4
    u16 addr = 0xB800, link = 1;
    s16 x = s16(R16(0xFF0A14) + 0x7C + R16(0xFF0A18)), y = s16(R16(0xFF0A16) + 0x6F + R16(0xFF0A1A));
    introSprite(addr, x, y, link, 0x458);
    x = s16(R16(0xFF0A18) + 0x80); y = s16(R16(0xFF0A1A) + 0x80);
    introSprite(addr, x, y, link, u16(0x494 + R16(0xFF0A1C)));
    x = s16(0xCA + R16(0xFF0A18)); y = s16(0x79 + R16(0xFF0A1A));
    introSprite(addr, x, y, link, u16(0x50C + 0x3C * R16(0xFF0A16)));
    ll::vramPoke(addr, 0);
    ll::vramPoke(u16(addr + 2), 0);
}

void fillWords(u32& a, int count, u16 v) {
    for (int i = 0; i < count; i++, a += 2) W16(a, v);
}

void introParallax() {   // 0x17AE: per-line scroll tables, bands moving at different speeds
    M.vdp.regWord(0x8F04);
    ll::dmaVram(0xFF3000, 0xBC00, 0xE0);
    ll::dmaVram(0xFF3800, 0xBC02, 0xE0);
    M.vdp.regWord(0x8F02);
    if (RS16(0xFF0A18) >= 0) W16(0xFF09F0, 0xFF00);
    const u16 still = R16(0xFF09F0);
    auto dec = [](u32 a, u32 by) { W32(a, R32(a) - by); };
    u32 b = 0xFF3800, a = 0xFF3000;
    if (R16(EpisodeIndex) != 0) {
        dec(0xFF0A10, 0x8000); dec(0xFF09F4, 0xC000); dec(0xFF09F8, 0x10000); dec(0xFF09FC, 0x10000);
        dec(0xFF0A00, 0x18000); dec(0xFF0A04, 0x20000); dec(0xFF0A08, 0x30000); dec(0xFF0A0C, 0x40000);
        fillWords(b, 0x68, still); fillWords(b, 0x20, R16(0xFF0A10)); fillWords(b, 0x19, R16(0xFF09F8));
        fillWords(b, 0x0F, R16(0xFF0A04)); fillWords(b, 0x30, R16(0xFF0A08));
        fillWords(a, 0x28, R16(0xFF09F4)); fillWords(a, 0x10, R16(0xFF09FC)); fillWords(a, 0x10, R16(0xFF0A00));
        fillWords(a, 0x98, R16(0xFF0A0C));
    } else {
        dec(0xFF09F4, 0x8000); dec(0xFF09F8, 0x10000); dec(0xFF09FC, 0x20000); dec(0xFF0A00, 0x40000);
        fillWords(b, 0x50, still);
        b = 0xFF38A0;
        fillWords(b, 0x90, R16(0xFF09FC));
        fillWords(a, 0x60, R16(0xFF09F4)); fillWords(a, 0x30, R16(0xFF09F8)); fillWords(a, 0x50, R16(0xFF0A00));
    }
}

void episodeIntro() {   // 0x13BA: title card with the van driving past
    const u32 savedVbl = R32(VBlankHandler);
    W32(VBlankHandler, irq::VblPlain);
    for (u16 r : {0x9100, 0x9200, 0x9003, 0x8D2F, 0x855C}) M.vdp.regWord(r);
    W32(HBlankHandler, irq::HblNone);
    M.vdp.regWord(0x8B03);
    const u32 set = 0x18FEC + (R16(EpisodeIndex) << 4);
    const u16 dest[3] = {0x0000, 0xE000, 0xC000};
    for (int i = 0; i < 3; i++) {
        ll::lzssDecode(R32(set + i * 4), 0xFF3000, 0xFF4000);
        ll::vramWrite(dest[i], u16(R32(0xFF3000)) >> 1, 0xFF4000);
    }
    const u32 van = 0x10B14;
    u32 list = van + R16(van + 4);
    u16 addr = 0x8B00;
    for (int i = 0; i < 6; i++, list += 2, addr = u16(addr + 0x780)) {
        ll::packbitsDecode(R32(van + R16(list) + 6) + van, 0xFF3000);
        ll::vramWrite(addr, 0x3C0, 0xFF3000);
    }
    introSprites();
    for (int i = 0; i < 0x400; i++) W32(0xFF3000 + i * 4, 0);
    M.vdp.vsram[0] = 0; M.vdp.vsram[1] = 0;
    W32(0xFF09F0, 0); W32(0xFF09F8, 0); W32(0xFF0A00, 0); W32(0xFF0A08, 0); W16(0xFF0A10, 0);
    W16(0xFF0A1A, 0x9F); W16(0xFF0A18, u16(-0x9E)); W8(0xFF0AD0, 1); W16(0xFF0A1C, 0); W32(0xFF0A14, 0);
    introParallax();
    const u32 pal = R32(set + 0x0C);
    for (int i = 0; i < 64; i++) W16(TargetPalette + 2 * i, R16(pal + 2 * i));
    for (int i = 0; i < 30; i++) W16(TargetPalette + 2 + 2 * i, R16(0x32482 + 2 * i));
    if (R16(EpisodeIndex) == 0) W16(TargetPalette, 0);
    introParallax();
    introSprites();
    snd::startSequence(0x7C);
    snd::startSequence(0x1E);
    ll::fadeIn(TargetPalette);
    W16(0xFF08AA, 0); W16(0xFF08AC, 0); W8(0xFF0ACF, 0);
    for (;;) {
        ll::waitVBlank();
        if (R8(0xFF0ACF) == 0) {   // the van's bounce
            u16 i = R16(0xFF08AA);
            W16(0xFF0A16, R16(0x160C + i));
            i = u16(i + 2);
            if (i == 0x16) i = 0;
            W16(0xFF08AA, i);
            if (i == 0) {
                u16 k = R16(0xFF08AC);
                W8(0xFF0ACF, R8(0x1622 + k));
                if (++k == 1) k = 0;
                W16(0xFF08AC, k);
            }
        } else {
            W8(0xFF0ACF, u8(R8(0xFF0ACF) - 1));
        }
        introParallax();
        introSprites();
        W8(0xFF0AD0, u8(R8(0xFF0AD0) - 1));
        if (RS8(0xFF0AD0) < 0) {
            W16(0xFF0A1C, R16(0xFF0A1C) ^ 0x3C);
            W8(0xFF0AD0, 1);
            W16(0xFF0A18, u16(R16(0xFF0A18) + 1));
        }
        const u8 now = R8(PadState) & 0xF0;
        if (now != 0xF0 && now != (R8(PadPrev) & 0xF0)) break;   // any button skips
        if (R16(0xFF0A18) == 0x10E) break;
    }
    ll::fadeOut();
    W16(TargetPalette, R16(0x32480));
    M.vdp.regWord(0x8B00);
    W32(HBlankHandler, irq::HblSplit);
    M.vdp.regWord(0x856C);
    M.vdp.regWord(0x8D37);
    M.vdp.regWord(0x9001);
    W32(VBlankHandler, savedVbl);
}

// 0x7F66: expands the episode's object definitions and shows its intro.
void loadEpisode() {
    const u32 hdr = R32(0x31AF2 + (R16(EpisodeIndex) << 2));
    W32(RoomHeaderPtr, hdr);
    u32 def = R32(hdr + 4);
    u32 o = 0xFF1200, carried = 0xFF0A1E;
    u16 index = 0xFFFF;
    do {
        for (int i = 0; i < 0x1A; i++) W8(o + i, 0);
        W32(o, R32(def)); W32(o + 4, R32(def + 4));
        W16(o + 8, R16(def + 8)); W16(o + 0x16, R16(def + 10)); W16(o + 0x18, R16(def + 12));
        def += 14;
        W16(o + 0x0A, 0);
        W8(o + 0x19, 0xFF);
        if (BTST(o + 0x18, 1)) { W32(o + 0x12, R32(def)); def += 4; }
        if (R16(o + 6) == 1) { W16(carried, u16(index + 1)); carried += 2; }
        index++;
        o += 0x1A;
    } while (def != R32(hdr + 8));
    W16(carried, 0xFFFF);
    W16(0xFF06F4, index);
    const u32 start = R32(hdr + 0x30);
    W16(0xFF06AC, R16(start));
    W32(0xFF06A6, u32(s32(RS16(start + 2))));
    vm::pc = start + 6;
    vm::end = vm::pc + RS16(start + 4);
    const u32 pc = vm::pc, end = vm::end;
    episodeIntro();
    snd::stopSequence(0x1E);
    vm::pc = pc;
    vm::end = end;
}

void dropVerbHighlights() {   // 0x1BDA
    BCLR(EngineFlags, 3);
    W16(0xFF06C2, R16(0xFF06C0));
    ui::highlightVerb(0);
    W16(0xFF06C2, R16(0xFF06C4));
    ui::highlightVerb(0);
}

// Addition, not in the original. In the first episode, using the bed spring
// hides the lead and bounces a stand-in for 600 ticks. The cartridge lets
// any verb run meanwhile, and every script that walks the lead then waits
// for ever for a hidden character to appear (the wait at 0x55F0), which locks
// the game up. So an action that is not part of the bounce ends the bounce
// first, by running the spring's own tick script at its final count.
void endBounceBeforeAction() {
    const u32 spring = 0xFF1200 + (90 - 3) * 0x1A;
    if (R16(EpisodeIndex) != 0 || !BTST(0xFF2A09, 0) || !BTST(0xFF09E8, 2)) return;
    const u16 verb = R16(0xFF06BE), object = R16(0xFF06AE);
    if (verb == 10 || verb == 0x0B || (verb == 2 && object == 53)) return;   // looking, grabbing the lights and the loop's own walk-on checks are fine in mid-air
    for (int tries = 0; tries < 4 && BTST(0xFF2A09, 0); tries++) {
        W16(spring + 0x0C, 600);
        BSET(spring + 0x18, 2);
        actor::objectTicks();
    }
}

// 0x19B0: runs the current verb on the object under the pointer.
void performAction() {
    endBounceBeforeAction();
    for (;;) {
        W16(0xFF080E, 0);
        BCLR(0xFF0ACA, 3);
        const u32 hdr = R32(RoomHeaderPtr);
        const s16 n = s16(R16(0xFF06AE) - 3);
        if (n < 0) return;
        if (R16(0xFF06BE) == 2 && R16(0xFF1200 + n * 0x1A + 6) == 1) return;
        const u32 block = R32(hdr + 0x2C) + R32(R32(hdr + 0x28) + s16(u16(n << 3)));
        W16(0xFF06B0, R16(block + 2));
        const u32 pc = block + 6, end = block + RS16(block);
        vm::pc = pc;
        vm::end = end;
        ui::collectChoices();
        if (R16(0xFF080E) != 0) dropVerbHighlights();
        W8(0xFF0AC9, 2);
        W32(0xFF0878, vm::ScanLoop);
        const u16 verb = R16(0xFF06BE);
        vm::verb = verb;
        const u8 wasPicking = R8(0xFF09DE) & 1;
        vm::runScan();
        if (BTST(0xFF0AC9, 0)) {
            BSET(0xFF0AC9, 6);
            vm::end = vm::pc + RS16(vm::pc + 6);
            vm::pc += 8;
            W32(0xFF0878, vm::ExecLoop);
            BCLR(0xFF0AC9, 0);
            dropVerbHighlights();
            vm::runExec();
        }
        vm::pc = pc;
        vm::end = end;
        if (verb == 8) {
            const u8 f = R8(0xFF0AC9);
            ui::dialogue();
            W8(0xFF0AC9, f);
            if (R16(0xFF080E) != 0) BSET(0xFF0AC9, 6);
        }
        if (!BTST(0xFF0AC9, 6)) {   // no script handled it
            if (verb == 5 || verb == 6) {
                bool cancel;
                if (verb == 5) { cancel = BTST(0xFF09E8, 0); BCLR(0xFF09E8, 0); }
                else { BSET(0xFF09E8, 0); cancel = wasPicking; }
                if (cancel) BCLR(0xFF09DE, 0);
                else if (!BTST(0xFF0AC9, 5)) {
                    if (BTST(0xFF09DE, 0)) BCLR(0xFF09DE, 0);
                    else { W16(0xFF06B6, 0); BSET(0xFF09DE, 0); }
                }
            }
            if (!BTST(0xFF09DE, 0) && verb != 0x0B) {
                // Stock refusal line for the verb, with a pronoun spliced in.
                u32 src = R32(0x2FEA0 + s16(u16((verb - 1) << 2))), dst = 0xFF08BC;
                for (;;) {
                    const u8 c = R8(src++);
                    W8(dst++, c);
                    if (!c) break;
                    if (c != 1) continue;
                    dst--;
                    u32 word = 0x2FFDB;
                    if (BTST(0xFF06B0, 0)) word = BTST(0xFF06B0, 1) ? 0x2FFE4 : 0x2FFE0;
                    for (;;) { const u8 w = R8(word++); W8(dst++, w); if (!w) break; }
                    dst--;
                }
                ui::showMessage(0xFF08BC);
            }
        }
        if (R16(0xFF06C0) != R16(0xFF06BE)) W16(0xFF06C0, 0);
        if (!BTST(0xFF0ACA, 3)) return;
        W16(0xFF06BE, 8);
        W16(0xFF06AE, R16(0xFF06DA));
    }
}

u32 hotspotRect(u16 index) {
    const u32 hdr = R32(RoomHeaderPtr);
    return R32(R32(hdr + 0x24) + s16(u16((index - 1) << 2))) + R32(hdr + 0x1C);
}

bool inRect(u32 r, s16 col, s16 row) {
    return col >= RS16(r) && row >= RS16(r + 2) && col < s16(RS16(r) + RS16(r + 4)) && row < s16(RS16(r + 2) + RS16(r + 6));
}

s16 defaultVerb(u16 objectNumber) {
    if (objectNumber < 3) return 10;
    return RS8(vm::objectAddr(objectNumber) + 0x16);
}

u16 carriedCount() {
    u16 n = 0;
    for (u32 p = 0xFF0A1E; RS16(p) >= 0; p += 2) n++;
    return n;
}

// What the pointer is over: 0 nothing, 2 the companion, 3 and up an object.
u16 pointerTarget() {
    const s16 px = s16(R16(CursorX) + R16(CameraX)), py = s16(R16(CursorY) + R16(CameraY));
    const s16 col = s16(u16(R16(CursorX) + 8 + R16(CameraX)) >> 3);
    const s16 row = s16(u16(R16(CursorY) - 8 + R16(CameraY)) >> 3);
    u16 hit = 0;
    auto nearActor = [&](s16 ax, s16 ay, u32 k) {
        return px <= s16(ax + RS16(0x32598 + k)) && px >= s16(ax - RS16(0x32550 + k)) &&
               py <= s16(ay + RS16(0x32508 + k)) && py >= s16(ay - RS16(0x324C0 + k));
    };
    if (!BTST(0xFF0ABF, 1) && BTST(0xFF0AB4, 1) && nearActor(RS16(0xFF04E0), RS16(0xFF04F8), 2)) hit = 2;
    u32 o = 0xFF1200;
    for (u16 n = 0; s16(n) <= RS16(0xFF06F4); n++, o += 0x1A) {
        if (R16(o + 6) != R16(0xFF06AC)) continue;
        if (BTST(o + 0x18, 1)) {
            const u32 s = u32(R8(o + 0x19)) << 2;
            if (nearActor(RS16(0xFF04E4 + s), RS16(0xFF04FC + s), u32(u16((R16(o + 2) - 1) * 2)))) hit = u16(n + 3);
        } else if (R16(o + 2) != 0 && inRect(hotspotRect(R16(o + 2)), col, row)) {
            hit = u16(n + 3);
        }
    }
    return hit;
}

void setTarget(u16 hit) {
    if (!BTST(0xFF09DE, 0)) { W16(0xFF06AE, hit); return; }
    if (hit == R16(0xFF06AE)) hit = 0;
    W16(0xFF06B6, hit);
}

// Direct control: standing on an object's rectangle runs its walk-on script
// once (0x12CC-0x13AC). Returns true if a script was run.
bool walkOnTrigger() {
    const s16 col = s16(R16(0xFF04DC) >> 3), row = s16(R16(0xFF04F4) >> 3);
    u16 hit = 0;
    u32 o = 0xFF1200;
    for (u16 n = 0; s16(n) <= RS16(0xFF06F4); n++, o += 0x1A)
        if (R16(o + 6) == R16(0xFF06AC) && !BTST(o + 0x18, 1) && R16(o + 2) != 0 && inRect(hotspotRect(R16(o + 2)), col, row))
            hit = u16(n + 3);
    W16(0xFF06AE, hit);
    if (!hit) return false;
    if (hit != R16(0xFF06B4)) BCLR(0xFF0AC9, 7);
    if (BTST(0xFF0AC9, 7)) return false;
    W16(0xFF06BE, 0x0B);
    BSET(EngineFlags, 7);
    performAction();
    BSET(0xFF0AC9, 7);
    BCLR(EngineFlags, 7);
    ll::waitVBlank();
    return true;
}

static void adventurePass();

void adventureLoop() {
    for (;;) {
        try { adventurePass(); }
        catch (const autoplay::Abort&) { autoplay::aborted(); }
    }
}

// One run of the original loop, until something restarts it.
static void adventurePass() {
    for (;;) {
        if (!BTST(EngineFlags, 7)) autoplay::idle();
        actor::objectTicks();
        if (BTST(EngineFlags, 7)) {
            performAction();
            // Addition for mouse play: a verb is used once, then dropped,
            // unless a two-object verb is still waiting for its second object.
            if (M.mouseEnabled && M.mouseActive && !BTST(0xFF09DE, 0) && R16(0xFF06C0) != 0) {
                W16(0xFF06C2, R16(0xFF06C0));
                ui::highlightVerb(0);
                W16(0xFF06C0, 0);
            }
            W16(0xFF06B6, 0);
            ll::waitVBlank();
            BCLR(EngineFlags, 7);
            ll::waitVBlank();
            continue;
        }
        if (!BTST(PadState, 7) && !BTST(0xFF09E9, 4)) {   // Start: pause menu
            BCLR(EngineFlags, 6);
            ll::fadeOut();
            menu::titleMenu();
            room::enterAndShow();
            BCLR(EngineFlags, 7);
            ll::waitVBlank();
            continue;
        }
        ui::refreshStatusLine();
        W16(0xFF06B4, R16(0xFF06AE));
        W8(0xFF09DD, R8(EngineFlags));
        W8(0xFF09DF, R8(0xFF09DE));
        W32(0xFF06D6, R32(0xFF06D2));
        W16(0xFF06B8, R16(0xFF06C0));
        W16(0xFF06BC, R16(0xFF06B6));

        // The companion wanders to a random spot now and then.
        if (!BTST(0xFF09DE, 7) && !BTST(0xFF0AB7, 1) && !BTST(0xFF09EB, 2) && RS16(0xFF0688) < 0 && BTST(0xFF0ABC, 1)) {
            W16(0xFF0688, 0x64);
            if (R16(0xFF06A0) != 0) {
                const u32 spot = R32(0xFF069A) + (ll::random(R16(0xFF06A0)) << 2);
                const u16 w = R16(spot);
                W16(0xFF0640, (w >> 12) & 3);
                W16(0xFF063E, R16(0xE8C + ((w >> 14) & 3) * 2));
                bool started;
                actor::companionWalk(u16((w & 0xFFF) << 3), u16(R16(spot + 2) << 3), 1, 0, started);
            }
        }

        if (!BTST(EngineFlags, 3)) {
            if (walkOnTrigger()) continue;
            actor::update();
            ll::idleFrame();   // the original spins this loop between interrupts
            continue;
        }

        if (M.walkRequest) {
            M.walkRequest = false;
            actor::clickWalk(M.walkX, M.walkY, M.walkObject);
            // Arriving on a walk-on rectangle triggers it, as it would on foot.
            W16(0xFF06B4, 0);
            if (walkOnTrigger()) continue;
        }
        bool settle = false, skipHover = false;   // 0x1222 and 0x1280
        if (RS16(CursorY) < 0xA8) {
            if (s16(RS16(CursorY) - 8) >= 0) setTarget(pointerTarget());
        } else {
            const u16 cell = R16(0xFF06EA);
            if (!BTST(EngineFlags, 4)) {   // verb panel
                if (cell != 0) {
                    if (BTST(PadState, 4)) settle = true;
                    else {
                        const u16 verb = R16(0x2FE3A + cell * 2);
                        if (R16(0xFF06C0) == 0) { W16(0xFF06C2, R16(0xFF06C4)); ui::highlightVerb(0); }
                        if (verb == R16(0xFF06C0)) settle = true;
                        else {
                            W16(0xFF06C2, R16(0xFF06C0));
                            ui::highlightVerb(0);
                            W16(0xFF06C0, verb);
                            BCLR(0xFF09DE, 0);
                            W16(0xFF06C2, verb);
                            ui::highlightVerb(1);
                            skipHover = true;
                        }
                    }
                }
            } else if (cell == 5 || cell == 10) {   // page arrows
                if (!BTST(0xFF09DE, 0)) W16(0xFF06AE, 0);
                else W16(0xFF06B6, 0);
                if (!BTST(PadState, 4)) {
                    if (cell == 10) {
                        const u16 count = carriedCount();
                        if (count && u16((count - 1) >> 2) != R16(0xFF06EC)) {
                            W16(0xFF06EC, u16(R16(0xFF06EC) + 1));
                            ui::setArrowDown(1);
                            ui::redrawInventory();
                        }
                        ui::updatePageArrows();
                    } else {
                        if (R16(0xFF06EC) != 0) {
                            W16(0xFF06EC, u16(R16(0xFF06EC) - 1));
                            ui::setArrowUp(1);
                            ui::redrawInventory();
                        }
                        ui::updatePageArrows();
                    }
                }
                settle = true;
            } else {   // an inventory cell
                u16 c = cell;
                if (s16(c) >= 5) c--;
                const u16 want = u16((R16(0xFF06EC) << 2) + c);
                u16 item = 0;
                if (s16(carriedCount()) >= s16(want)) item = u16(R16(0xFF0A1E + s16(u16(want * 2)) - 2) + 3);
                setTarget(item);
            }
        }
        if (settle) {
            W16(0xFF06EA, 0);
            ll::waitVBlank();
        }
        if (!skipHover) {
            const u16 prev = R16(0xFF06B4);
            if (prev != R16(0xFF06AE) && prev != 0) {
                const s16 v = defaultVerb(prev);
                if (v != RS16(0xFF06C0)) { W16(0xFF06C2, u16(v)); ui::highlightVerb(0); }
            }
        }
        if (R16(0xFF06C0) == 0 && R16(0xFF06AE) != 0) {
            const s16 v = defaultVerb(R16(0xFF06AE));
            W16(0xFF06C4, u16(v));
            if (v != 0) { W16(0xFF06C2, u16(v)); ui::highlightVerb(2); }
        }
        actor::update();
        ll::idleFrame();
    }
}

}  // namespace

void titleAndRun() {
    menu::titleMenu();
    snd::stopAll();
    resetGameState();
    loadEpisode();
    actor::initLeads();
    BSET(EngineFlags, 7);
    BCLR(0xFF0ACA, 1); BCLR(0xFF0ACA, 0);
    vm::runExec();
    BCLR(0xFF0ACA, 1); BCLR(0xFF0ACA, 0);
    const s16 len = RS16(vm::end);
    vm::pc = vm::end + 2;
    vm::end = vm::pc + len;
    vm::runExec();
    BCLR(EngineFlags, 7);
    if (BTST(0xFF09ED, 2)) {   // continuing from a password
        W16(0xFF069E, 0);
        W16(0xFF06AC, R16(0xFF0A90));
        room::enter();
    }
    room::show();
    ui::closeTextBox();
    if (M.debugRoom) {   // testing aid (--room)
        for (const auto& p : M.debugPokes) W8(p.first, p.second);
        W16(0xFF069E, 0);
        W16(0xFF06AC, u16(M.debugRoom));
        { const u8 f = R8(EngineFlags); BCLR(EngineFlags, 6); ll::fadeOut(); W8(EngineFlags, f); }
        room::enterAndShow();
    }
    adventureLoop();
}

}  // namespace game
