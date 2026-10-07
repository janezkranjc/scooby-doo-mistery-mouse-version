#include "lowlevel.h"
#include "autoplay.h"
#include "addr.h"

namespace ll {

// 0x974C. The game raises a flag and spins until the frame interrupt clears
// it. Here the wait hands the frame to the presentation loop instead.
bool allowSkip = false;

void waitVBlank() {
    BSET(EngineFlags, 0);
    M.yieldFrame();
    autoplay::checkAbort();
    if (allowSkip && M.skipRequested) {
        M.skipRequested = false;
        throw SkipScreens{};
    }
}

// The original has several loops that spin on a counter the frame interrupt
// decrements. Each pass of such a loop is one call to this.
void idleFrame() { M.yieldFrame(); autoplay::checkAbort(); }

// 0x9742: dbf loop, so `count` + 1 frames.
void waitFrames(u16 count) {
    for (u32 i = 0; i <= count; i++) waitVBlank();
}

static inline u16 autoInc() { return M.vdp.reg[15]; }

// 0x992C
u32 vramWrite(u16 addr, u16 words, u32 src) {
    const u16 step = autoInc();
    for (u32 i = 0; i < words; i++) {
        M.vdp.vramPoke(addr, R16(src));
        src += 2;
        addr = u16(addr + step);
    }
    return src;
}

void vramPoke(u16 addr, u16 value) { M.vdp.vramPoke(addr, value); }   // 0x9964
u16 vramPeek(u16 addr) { return M.vdp.vramWord(addr); }               // 0x9996
void cramPoke(u16 addr, u16 value) { M.vdp.cramPoke((addr & 0x7F) >> 1, value); }   // 0x99C4

// 0x99F6 / 0x9A3C. The original splits a transfer that crosses a 64 KB source
// boundary into two DMA operations. The copy here has no such limit.
u32 dmaVram(u32 src, u16 addr, u16 words) {
    const u16 step = autoInc();
    for (u32 i = 0; i < words; i++) {
        M.vdp.vramPoke(addr, R16(src));
        src += 2;
        addr = u16(addr + step);
    }
    return src;
}

// 0x9B10
u32 dmaCram(u32 src, u16 addr, u16 words) {
    for (u32 i = 0; i < words; i++) {
        M.vdp.cramPoke(((addr & 0x7F) >> 1) + int(i), R16(src));
        src += 2;
    }
    return src;
}

// 0x9BE4. A count of zero wraps to 65536 words in the original loop.
void vramFill(u16 addr, u16 words, u16 value) {
    const u16 step = autoInc();
    u32 n = words ? words : 0x10000;
    for (u32 i = 0; i < n; i++) {
        M.vdp.vramPoke(addr, value);
        addr = u16(addr + step);
    }
}

// 0x9C1E
void cramFill(u16 addr, u16 words, u16 value) {
    for (u32 i = 0; i < words; i++) M.vdp.cramPoke(((addr & 0x7F) >> 1) + int(i), value);
}

void vramClear(u16 addr, u16 words) { vramFill(addr, words, 0); }   // 0x9C58
void cramClear(u16 addr, u16 words) { cramFill(addr, words, 0); }   // 0x9C5E

// 0x972E
void setVdpReg(u16 reg, u16 value) { M.vdp.setReg(reg & 0x7F, u8(value)); }

// 0x97AA. An LZSS variant: MSB-first bit stream, 4096-byte ring whose write
// position starts at 1. A flag bit of 1 is followed by an 8-bit literal. A
// flag bit of 0 is followed by a 12-bit ring position (0 ends the stream) and
// a 4-bit length code, copying code+2 bytes from that position.
void lzssDecode(u32 src, u32 ring, u32 dst) {
    u32 bitPos = 0;
    auto bits = [&](int n) {
        u32 v = 0;
        for (int i = 0; i < n; i++, bitPos++)
            v = v << 1 | ((R8(src + (bitPos >> 3)) >> (7 - (bitPos & 7))) & 1);
        return v;
    };
    const u32 start = dst;
    u16 wpos = 1;
    for (;;) {
        if (bits(1)) {
            const u8 b = u8(bits(8));
            W8(dst++, b);
            W8(ring + wpos, b);
            wpos = (wpos + 1) & 0xFFF;
        } else {
            u16 pos = u16(bits(12));
            if (pos == 0) break;
            const u16 last = u16(pos + bits(4) + 1);
            for (; pos <= last; pos++) {
                const u8 b = R8(ring + (pos & 0xFFF));
                W8(dst++, b);
                W8(ring + wpos, b);
                wpos = (wpos + 1) & 0xFFF;
            }
        }
    }
    W32(ring, dst - start);
}

// 0x9C64. PackBits-style run-length coding with a 32-bit size header of which
// only the low word is used. Runs are written a long at a time and then the
// pointer is pulled back, so up to three bytes past a run are overwritten
// before the following data lands on them. That detail is kept so the RAM
// image matches the original byte for byte.
void packbitsDecode(u32 src, u32 dst) {
    const u32 end = dst + s16(R16(src + 2));
    src += 4;
    while (dst != end) {
        const u8 n = R8(src++);
        if (!(n & 0x80)) {
            for (int i = 0; i <= n; i++) W8(dst++, R8(src++));
        } else {
            const u8 value = R8(src++);
            u16 count = u8(-n);
            if (dst & 1) {
                W8(dst++, value);
                count = u16(count - 1);
            }
            const u32 fill = value * 0x01010101u;
            const u16 back = (count & 3) ^ 3;
            for (u32 i = 0; i <= u32(count >> 2); i++) {
                W32(dst, fill);
                dst += 4;
            }
            dst -= back;
        }
    }
}

// 0x9DE0. Reads the live palette back, then darkens every component by one
// level per step for eight steps.
void fadeOut() {
    waitVBlank();
    for (int i = 0; i < 64; i++) W16(WorkPalette + 2 * i, M.vdp.cram[i]);
    for (int step = 0; step < 8; step++) {
        for (int i = 0; i < 64; i++) {
            const u32 a = WorkPalette + 2 * i;
            const u16 c = R16(a) & 0x0EEE;
            u16 v = c;
            if (c & 0xF00) v = u16(v - 0x200);
            if (c & 0x0F0) v = u16(v - 0x020);
            if (c & 0x00F) v = u16(v - 0x002);
            W16(a, v);
        }
        waitFrames(0);
        dmaCram(WorkPalette, 0, 0x40);
    }
}

// 0x9E7E. Starts from black and raises every component by one level per step
// until it matches the target.
void fadeIn(u32 target) {
    for (int i = 0; i < 64; i++) W16(WorkPalette + 2 * i, 0);
    for (int step = 0; step < 8; step++) {
        for (int i = 63; i >= 0; i--) {
            const u32 a = WorkPalette + 2 * i;
            const u16 c = R16(a) & 0x0EEE;
            const u16 t = R16(target + 2 * i);
            u16 v = c;
            if ((t & 0xE00) != (c & 0xE00)) v = u16(v + 0x200);
            if ((t & 0x0E0) != (c & 0x0E0)) v = u16(v + 0x020);
            if ((t & 0x00E) != (c & 0x00E)) v = u16(v + 0x002);
            W16(a, v);
        }
        waitFrames(0);
        dmaCram(WorkPalette, 0, 0x40);
    }
}

// 0x9D34. Stores each pad as one byte, active low, bits Start A C B R L D U.
// The previous pad-1 state is kept, and changed bits accumulate in the
// "edge" bytes.
void readPads() {
    W16(PadPrev, R16(PadState));
    W8(PadState, u8(~M.pad1));
    W8(PadState + 1, u8(~M.pad2));
    W8(PadEdge, R8(PadEdge) | (R8(PadLatch) ^ R8(PadState)));
    W8(PadEdge + 1, R8(PadEdge + 1) | (R8(PadLatch + 1) ^ R8(PadState + 1)));
}

// 0x22D6. Rotate-and-invert generator. Returns a value in [0, range).
u16 random(u16 range) {
    u32 seed = R32(RandomSeed);
    seed = (seed << 1 | seed >> 31) ^ 0xFFFFFFFFu;
    W32(RandomSeed, seed);
    return u16((u32(range) * (seed & 0xFFFF)) >> 16);
}

u16 strLen(u32 s) {   // 0xA236
    u16 n = 0;
    while (R8(s++)) n++;
    return n;
}

// 0x9F72. Text on plane A of a 32-cell-wide map; characters are tile numbers
// offset by the attribute base.
u32 drawString(u32 s, u16 col, u16 row) {
    u16 addr = u16(0xC000 + col * 2 + row * 64);
    while (u8 ch = R8(s++)) {
        vramPoke(addr, u16(ch + R16(TextAttrBase)));
        addr = u16(addr + 2);
    }
    return s;
}

// 0x9F58
u32 drawStringHigh(u32 s, u16 col, u16 row) {
    const u16 saved = R16(TextAttrBase);
    BSET(TextAttrBase, 7);
    s = drawString(s, col, row);
    W16(TextAttrBase, saved);
    return s;
}

// 0x9F46: draws a row of blanks from 0x2FFF4 at column 0, then the string.
u32 drawStringCleared(u32 s, u16 col, u16 row) {
    drawStringHigh(0x2FFF4, 0, row);
    return drawStringHigh(s, col, row);
}

// 0x7A2A. The font at 0x30F12 is 95 glyphs of 16 words. Zero pixels take the
// background colour and every other pixel the foreground colour.
void loadFont(u16 fg, u16 bg, u16 attrBase) {
    u16 addr = u16(((attrBase & 0x7FF) + 0x20) << 5);
    u32 src = 0x30F12;
    for (int i = 0; i < 0x5F0; i++) {
        u16 w = R16(src);
        src += 2;
        for (int n = 0; n < 4; n++) {
            const int sh = 12 - 4 * n;
            const u16 mask = u16(0xF << sh);
            w = u16((w & mask) ? ((w & ~mask) | (fg << sh)) : (w | (bg << sh)));
        }
        vramPoke(addr, w);
        addr = u16(addr + 2);
    }
}

// 0x96F4: decodes a full 32x28 map and puts it on plane A.
void showPackedScreen(u32 src) {
    packbitsDecode(src, 0xFF3000);
    waitVBlank();
    dmaVram(0xFF3000, 0xC000, 0x380);
}

}  // namespace ll
