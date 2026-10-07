#include "scaler.h"
#include "../core/machine.h"

namespace scaler {

namespace {

constexpr u32 kMaskTable = 0xC3F0;
constexpr u32 kWaveStart = 0x8828, kWaveEnd = 0x8876;

inline bool maskBit(u32 base, int i) { return (R8(base + (i >> 3)) >> (7 - (i & 7))) & 1; }

enum class Wave { None, Negated, Plain };

void scale(u32 out, u32 src, int width, int rows, u32 layout, Wave wave) {
    const u32 rowMask = kMaskTable + s16(u16(R16(0xFF0626) << 4));
    const u32 colMask = kMaskTable + s16(u16(R16(0xFF0624) << 4));
    u32 wavePtr = kWaveStart + R16(0xFF000C);
    u32 acc = 0;
    int emittedRows = 0;
    auto put = [&](u32 v) {
        W32(out + s16(R16(layout)), v);
        layout += 2;
    };
    for (int r = 0; r < rows; r++, src += u32(width)) {
        s16 shift = 0;
        if (wave != Wave::None) {
            shift = RS16(wavePtr);
            wavePtr += 2;
            if (wavePtr >= kWaveEnd) wavePtr = kWaveStart;
            if (wave == Wave::Negated) shift = s16(-shift);
        }
        if (!maskBit(rowMask, r)) continue;
        u32 p = src;
        if (wave != Wave::None && BTST(0xFF09EB, 4)) p += shift;
        int emitted = 0;
        for (int c = 0; c < width; c++) {
            const u8 px = R8(p + c);
            if (!maskBit(colMask, c)) continue;
            acc = (acc << 4) | px;
            if ((++emitted & 7) == 0) put(acc);
        }
        const int remaining = width - emitted;
        if (remaining) {
            const int part = remaining & 7;
            if (part) {
                acc <<= part * 4;
                put(acc);
            }
            for (int i = 0; i < (remaining >> 3); i++) {
                acc = 0;
                put(0);
            }
        }
        emittedRows++;
    }
    for (int r = emittedRows; r < rows; r++)
        for (int g = 0; g < width / 8; g++) put(0);
}

// 0xD3F0 / 0xD42C: straight repack of 480 eight-pixel strips.
void copy(u32 out, u32 layout) {
    u32 src = 0xFF6000;
    u16 hi = 0, lo = 0;
    for (int i = 0; i < 480; i++) {
        for (u16* w : {&hi, &lo}) {
            *w = u16((*w & 0xFF00) | R8(src++));
            for (int k = 0; k < 3; k++) {
                *w = u16(*w << 4);
                *w = u16(*w | R8(src++));
            }
        }
        const u32 a = out + s16(R16(layout));
        layout += 2;
        W16(a, hi);
        W16(a + 2, lo);
    }
}

}  // namespace

void actorTall(u32 out) { scale(out, 0xFF6000, 48, 80, 0xBC70, Wave::None); }
void actorWide(u32 out) { scale(out, 0xFF6000, 80, 48, 0xC030, Wave::None); }
void copyTall(u32 out) { copy(out, 0xBC70); }
void copyWide(u32 out) { copy(out, 0xC030); }
void titleLogoFront() { scale(0xFFE000, 0xFF3800, 120, 40, 0xB5C0, Wave::Negated); }
void titleLogoBack() { scale(0xFFC400, 0xFF5000, 64, 32, 0xBA70, Wave::Plain); }

}  // namespace scaler
