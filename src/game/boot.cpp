// Boot sequence (0x690, 0x8CE-0xBC6) and the opening animation (0xBF4-0xCB4).
#include "addr.h"
#include "autoplay.h"
#include "game.h"
#include "interrupts.h"
#include "lowlevel.h"
#include "menu.h"
#include "sound.h"

namespace game {

void initState();        // 0xA248
void resetGameState();   // 0xA27A

// Decodes an LZSS tile set into the scratch buffer and uploads it to VRAM 0.
static void loadTilesLzss(u32 src) {
    ll::lzssDecode(src, 0xFF3000, 0xFF4000);
    ll::vramWrite(0, u16(R32(0xFF3000)) >> 1, 0xFF4000);
}

// Writes `rows` rows of `cols` map words, one plane row (0x80 bytes) apart.
static u32 writeMapRows(u16 addr, u32 src, u16 cols, int rows) {
    for (int r = 0; r < rows; r++) {
        src = ll::vramWrite(addr, cols, src);
        addr = u16(addr + 0x80);
    }
    return src;
}

// 0xA248
void initState() {
    W16(EpisodeIndex, 0);
    W32(RandomSeed, 0x45C3F2D1);
    ll::random(0);
    W8(0xFF09ED, 0);
    resetGameState();
}

// 0xA27A
void resetGameState() {
    W16(0xFF06EC, 0); W16(0xFF069E, 0); W16(0xFF0800, 0); W16(0xFF06AC, 0);
    W16(0xFF065A, 0xFFFF); W16(0xFF0670, 0xFFFF);
    W8(0xFF0ABE, 0);
    W8(PadState, 0xFF); W8(PadPrev, 0xFF); W8(PadState + 1, 0xFF); W8(PadPrev + 1, 0xFF);
    W16(0xFF080C, 4);
    W32(0xFF0692, 0);
    W16(CameraX, 0); W16(CameraY, 0);
    W16(0xFF0688, 0); W16(0xFF0804, 0); W16(0xFF0806, 0);
    W8(EngineFlags, 0); W8(0xFF09DE, 0);
    W8(0xFF09E8, 0); W8(0xFF09E9, 0); W8(0xFF09EA, 0); W8(0xFF09EB, 0);
    W8(0xFF0AB3, 0); W8(0xFF0AC9, 0); W8(0xFF0ACA, 0); W8(0xFF0AB1, 0); W8(0xFF0AB2, 0);
    W8(0xFF09EE, R8(0x10124));
    W16(ScrollAX, 0); W16(ScrollBX, 0); W16(ScrollAY, 0); W16(ScrollBY, 0);
    W16(0xFF06AE, 0);
    W8(0xFF0AC7, 0);
    W8(0xFF0ACB, 0xFF);
    for (int i = 0; i < 32; i++) W16(TargetPalette + 2 * i, R16(0x32480 + 2 * i));
}

// Rotates working-palette entries 3..10 by one place (0x9BC-0x9D6).
static void rotateLogoColours() {
    const u16 last = R16(0xFF070C);
    for (u32 a = 0xFF070C; a > 0xFF06FE; a -= 2) W16(a, R16(a - 2));
    W16(0xFF06FE, last);
}

void bootScreens() {
    W32(VBlankHandler, irq::VblMain);
    W32(HBlankHandler, irq::HblSplit);
    snd::init(0x1BD1D0, 0x1BE31F, 0x1BE496, 0x1C70C5);
    W32(0xFF06A6, 0x2F);
    snd::startSequence(0x2F);
    initState();
    ll::vramClear(0, 0x8000);
    ll::cramClear(0, 0x40);
    for (int r = 0x12; r >= 0; r--) M.vdp.setReg(r, R8(0x10123 + r));
    W32(HBlankHandler, irq::HblNone);

    // Publisher logo with a colour-cycling shine.
    writeMapRows(0xC594, 0x23226, 0x0C, 4);
    loadTilesLzss(0x23286);
    ll::fadeIn(0x236A0);
    ll::waitFrames(0x32);
    for (int i = 0; i < 8; i++) {
        rotateLogoColours();
        ll::waitVBlank();
        ll::dmaCram(WorkPalette, 0, 0x40);
        ll::waitFrames(2);
    }
    ll::waitFrames(0x64);
    ll::fadeOut();
    ll::vramClear(0, 0x8000);

    // Licence text, the only screen in the 320-pixel mode.
    M.vdp.regWord(0x8C81);
    writeMapRows(0xC000, 0x278E0, 0x28, 0x1C);
    ll::vramWrite(0, 0x20E0, 0x23720);
    ll::fadeIn(0x281A0);
    ll::waitFrames(0x12C);
    ll::fadeOut();
    ll::vramClear(0, 0x8000);
    M.vdp.regWord(0x8C00);

    // Developer logo sliding in on plane B.
    BSET(0xFF09EB, 1);
    W16(ScrollAX, 0);
    writeMapRows(0xC480, 0x219C0, 0x20, 6);
    ll::vramWrite(0xE480, 0x180, 0x21B40);
    loadTilesLzss(0x21E40);
    W16(ScrollBX, 0xFF00);
    ll::fadeIn(0x2241A);
    for (s16 x = -0x100;;) {
        ll::waitVBlank();
        x = s16(x + 4);
        W16(ScrollBX, u16(x));
        if (x >= 0x30) break;
    }
    ll::waitFrames(0xC8);
    ll::fadeOut();
    ll::vramClear(0, 0x8000);
    BCLR(0xFF09EB, 1);

    // Licensor logo.
    writeMapRows(0xC406, 0x2249A, 0x1A, 9);
    loadTilesLzss(0x2266E);
    ll::fadeIn(0x231A6);
    ll::waitFrames(0x12C);
    ll::fadeOut();
    ll::vramClear(0, 0x8000);
}

// 0xBC8: shows frame `n` of the opening animation (5 rows of 20 cells).
static void showOpeningFrame(u16 n) {
    u32 src = 0xFF4000 + u32(n) * 0xC8;
    u16 addr = 0xC410;
    for (int r = 0; r < 5; r++) {
        src = ll::dmaVram(src, addr, 0x14);
        addr = u16(addr + 0x80);
    }
}

void openingAnimation() {   // 0xBF4-0xCB0
    loadTilesLzss(0x287C8);
    ll::lzssDecode(0x28220, 0xFF3000, 0xFF4000);
    ll::lzssDecode(0x2864C, 0xFF3000, 0xFF5000);
    showOpeningFrame(0);
    ll::fadeIn(0x2967A);
    for (int loop = 0; loop < 4; loop++)
        for (u16 f = 0; f < 15; f++) {
            ll::waitFrames(2);
            showOpeningFrame(f);
        }
    writeMapRows(0xC288, 0xFF5000, 0x19, 15);
    ll::waitFrames(0xC8);
    ll::fadeOut();
    ll::vramClear(0, 0x8000);
}

void openingAnimation();

void entry() {
    // 0x69A probes port 2 for a factory test fixture and returns at once on
    // a retail console, so there is nothing to carry over.
    for (;;) {
        try {
            // Addition: any button skips the publisher screens and opening.
            try {
                ll::allowSkip = true;
                bootScreens();
                openingAnimation();
            } catch (const ll::SkipScreens&) {
                ll::vramClear(0, 0x8000);
                ll::cramClear(0, 0x40);
                M.vdp.regWord(0x8C00);
                BCLR(0xFF09EB, 1);
                W16(ScrollAX, 0); W16(ScrollBX, 0);
            }
            ll::allowSkip = false;
            titleAndRun();
        } catch (const menu::Restart&) {
            // quitting from the pause menu or finishing the credits restarts the program
            autoplay::programRestarted();
            if (autoplay::done) for (;;) M.yieldFrame();
        }
    }
}

}  // namespace game
