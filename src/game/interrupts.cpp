#include "interrupts.h"
#include "addr.h"
#include "lowlevel.h"

namespace irq {

// 0xFA78. Derives the hardware scroll values from the camera position and
// writes them to the scroll table and vertical scroll RAM.
void updateScroll() {
    if (!BTST(0xFF09EB, 1)) {
        if (BTST(0xFF09E9, 5)) {   // automatic horizontal pan in progress
            const s16 speed = RS16(0xFF08B4);
            const u16 x = u16(R16(CameraX) + speed);
            W16(CameraX, x);
            if ((x & 7) == ((8 + speed) & 7)) BSET(0xFF0AB3, speed < 0 ? 1 : 0);
            if (x == R16(0xFF08B6)) BCLR(0xFF09E9, 5);
        }
        u16 d0 = u16(-(R16(CameraX) & 0x3FF));
        W16(ScrollBX, d0);
        if (BTST(0xFF09DE, 6)) d0 = u16(R16(0xFF04DC) - R16(CameraX) - 0x2C);
        W16(ScrollAX, d0);
        d0 = R16(CameraY) & 0xFF;
        W16(ScrollBY, d0);
        if (BTST(0xFF09DE, 6)) d0 = u16(-(R16(0xFF04F4) - R16(CameraY) - 0x30));
        W16(ScrollAY, d0);

        if (BTST(0xFF09E8, 7)) {   // screen shake
            bool apply = true;
            if (RS16(0xFF08AA) < 0) {
                W8(0xFF0ACF, u8(R8(0xFF0ACF) - 1));
                if (RS8(0xFF0ACF) >= 0) {
                    apply = false;
                } else {
                    s16 idx = RS8(0xFF08AC);
                    W8(0xFF0ACF, R8(0x3247C + idx));
                    if (++idx == 4) idx = 0;
                    W8(0xFF08AC, u8(idx));
                    W16(0xFF08AA, 0);
                }
            }
            if (apply) {
                const u16 i = R16(0xFF08AA);
                W16(0xFF08AA, u16(i + 2));
                if (R16(0xFF08AA) == 0x2C) W16(0xFF08AA, 0xFFFF);
                W16(ScrollAY, u16(R16(ScrollAY) + R16(0x3244E + i)));
                W16(ScrollBY, u16(R16(ScrollBY) + R16(0x3244E + i + 2)));
            }
        }
        // Bits 0-3 of 0xFF0AB3 request fresh map columns or rows. Those
        // routines belong to the room code and are wired in with it.
        extern void roomScrollUpdates();
        roomScrollUpdates();
        W8(0xFF0AB3, 0);
    }
    ll::vramPoke(0xDC00, R16(ScrollAX));
    ll::vramPoke(0xDC02, R16(ScrollBX));
    M.vdp.vsram[0] = R16(ScrollAY);
    M.vdp.vsram[1] = R16(ScrollBY);
}

// Shared tail of every frame handler (0xA4A4 / 0xA6C0).
static void padsAndTimers() {
    ll::readPads();
    if (!BTST(PadState, 7)) BSET(0xFF0ACA, 0);        // Start held
    if (RS8(0xFF0AC7) >= 0) W8(0xFF0AC7, u8(R8(0xFF0AC7) - 1));
    BCLR(EngineFlags, 0);                             // releases waitVBlank
    if (RS16(0xFF08AE) >= 0) W16(0xFF08AE, u16(R16(0xFF08AE) - 1));
}

extern void gameFrameWork();    // 0xA684-0xA6BC, defined with the gameplay code
extern void titleFlashWork();   // 0xA38C-0xA4A0

void vblank() {
    switch (R32(VBlankHandler)) {
        case VblMain:
            M.vdp.regWord(0x8230);
            M.vdp.regWord(0x8407);
            M.vdp.regWord(0x856C);
            updateScroll();
            if (BTST(EngineFlags, 6)) gameFrameWork();
            padsAndTimers();
            break;
        case VblTitle:
            titleFlashWork();
            padsAndTimers();
            break;
        case VblPlain:
        default:
            padsAndTimers();
            break;
    }
}

// 0x1006C. The original waits for line 167 and then, during the following
// line, points both planes at the panel's tile map, zeroes vertical scroll
// and applies the panel's horizontal scroll. The window height for the
// dialogue text is then restored.
void beforeLine(int y) {
    if (y != 168) return;
    if (!(M.vdp.reg[0] & 0x10)) return;
    if (R32(HBlankHandler) != HblSplit) return;
    M.vdp.vsram[0] = 0;
    M.vdp.vsram[1] = 0;
    M.vdp.regWord(0x8228);
    M.vdp.regWord(0x8405);
    ll::vramPoke(0xDC00, R16(0xFF0800));
    ll::vramPoke(0xDC02, R16(0xFF0800));
    M.vdp.setReg(0x12, R8(0xFF0AD1));
}

}  // namespace irq
