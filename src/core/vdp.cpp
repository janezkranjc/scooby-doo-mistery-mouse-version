#include "vdp.h"
#include <algorithm>
#include <cstring>

void Vdp::reset() {
    vram.fill(0); cram.fill(0); vsram.fill(0); reg.fill(0);
}

void Vdp::vramWrite(uint16_t a, const uint8_t* src, int words, int step) {
    for (int i = 0; i < words; i++) {
        vram[a] = src[2 * i];
        vram[uint16_t(a + 1)] = src[2 * i + 1];
        a = uint16_t(a + step);
    }
}

void Vdp::vramFill(uint16_t a, uint16_t v, int words) {
    for (int i = 0; i < words; i++) { vramPoke(a, v); a = uint16_t(a + 2); }
}

uint32_t Vdp::toRgba(uint16_t c) {
    // Measured output levels of the console's 3-bit DAC.
    static const uint8_t lv[8] = {0, 52, 87, 116, 144, 172, 206, 255};
    uint8_t r = lv[(c >> 1) & 7], g = lv[(c >> 5) & 7], b = lv[(c >> 9) & 7];
    return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | 0xFF000000u;
}

// Fetches one pixel (0..15) of a tile row, honouring flips.
static inline uint8_t tilePixel(const uint8_t* vram, int tile, int px, int py, bool hf, bool vf) {
    if (hf) px = 7 - px;
    if (vf) py = 7 - py;
    uint8_t b = vram[(tile * 32 + py * 4 + (px >> 1)) & 0xFFFF];
    return (px & 1) ? (b & 15) : (b >> 4);
}

void Vdp::planeLine(int y, bool planeA, Px* buf, int w) const {
    static const int sizes[4] = {32, 64, 64, 128};
    const int pw = sizes[reg[16] & 3], ph = sizes[(reg[16] >> 4) & 3];
    const uint16_t nt = planeA ? uint16_t((reg[2] & 0x38) << 10) : uint16_t((reg[4] & 0x07) << 13);
    const uint16_t hsTab = uint16_t((reg[13] & 0x3F) << 10);

    int hsRow = 0;
    switch (reg[11] & 3) {
        case 0: hsRow = 0; break;
        case 2: hsRow = y & ~7; break;
        case 3: hsRow = y; break;
        default: hsRow = y & 7; break;   // invalid mode 1 reads the first 8 lines
    }
    const int hscroll = vramWord(uint16_t(hsTab + hsRow * 4 + (planeA ? 0 : 2))) & 0x3FF;
    const bool vs2cell = reg[11] & 4;

    for (int x = 0; x < w; x++) {
        const int vscroll = vsram[(vs2cell ? (x >> 4) * 2 : 0) + (planeA ? 0 : 1)] & 0x3FF;
        const int sx = (x - hscroll) & (pw * 8 - 1);
        const int sy = (y + vscroll) & (ph * 8 - 1);
        const uint16_t e = vramWord(uint16_t(nt + ((sy >> 3) * pw + (sx >> 3)) * 2));
        const uint8_t p = tilePixel(vram.data(), e & 0x7FF, sx & 7, sy & 7, e & 0x800, e & 0x1000);
        buf[x].color = uint8_t(((e >> 13) & 3) << 4 | p);
        buf[x].pri = uint8_t(e >> 15);
    }
}

bool Vdp::windowSpan(int y, int w, int& x0, int& x1) const {
    const int cellY = y >> 3;
    const int vp = reg[18] & 0x1F;
    const bool down = reg[18] & 0x80;
    const bool inV = down ? (cellY >= vp) : (cellY < vp);
    if (inV) { x0 = 0; x1 = w; return true; }
    const int hp = (reg[17] & 0x1F) * 16;
    const bool right = reg[17] & 0x80;
    if (right) { x0 = std::min(hp, w); x1 = w; }
    else { x0 = 0; x1 = std::min(hp, w); }
    return x1 > x0;
}

void Vdp::windowLine(int y, Px* buf, int w, int x0, int x1) const {
    const bool h40 = reg[12] & 1;
    const uint16_t nt = uint16_t((reg[3] & (h40 ? 0x3C : 0x3E)) << 10);
    const int pw = h40 ? 64 : 32;
    for (int x = x0; x < x1 && x < w; x++) {
        const uint16_t e = vramWord(uint16_t(nt + ((y >> 3) * pw + (x >> 3)) * 2));
        const uint8_t p = tilePixel(vram.data(), e & 0x7FF, x & 7, y & 7, e & 0x800, e & 0x1000);
        buf[x].color = uint8_t(((e >> 13) & 3) << 4 | p);
        buf[x].pri = uint8_t(e >> 15);
    }
}

void Vdp::spriteLine(int y, Px* buf, int w) const {
    const bool h40 = reg[12] & 1;
    const uint16_t sat = uint16_t((reg[5] & (h40 ? 0x7E : 0x7F)) << 9);
    const int maxSprites = h40 ? 80 : 64;
    int lineSprites = unlimitedSprites ? 1 << 30 : (h40 ? 20 : 16);
    int linePixels = unlimitedSprites ? 1 << 30 : (h40 ? 320 : 256);
    bool masked = false, sawNonZeroX = false;

    int link = 0;
    for (int n = 0; n < maxSprites; n++) {
        const uint16_t a = uint16_t(sat + link * 8);
        const int sy = (vramWord(a) & 0x3FF) - 128;
        const uint8_t size = vram[uint16_t(a + 2)];
        const int next = vram[uint16_t(a + 3)] & 0x7F;
        const int wc = ((size >> 2) & 3) + 1, hc = (size & 3) + 1;

        if (y >= sy && y < sy + hc * 8) {
            if (lineSprites-- <= 0) break;
            const uint16_t attr = vramWord(uint16_t(a + 4));
            const int xraw = vramWord(uint16_t(a + 6)) & 0x1FF;
            if (xraw == 0) {
                if (sawNonZeroX) masked = true;   // x = 0 masks lower-priority sprites
            } else {
                sawNonZeroX = true;
            }
            const int sx = xraw - 128;
            const bool hf = attr & 0x800, vf = attr & 0x1000;
            const uint8_t pal = uint8_t(((attr >> 13) & 3) << 4);
            const uint8_t pri = uint8_t(attr >> 15);
            int row = y - sy;
            if (vf) row = hc * 8 - 1 - row;
            for (int cx = 0; cx < wc * 8; cx++) {
                if (linePixels-- <= 0) break;
                if (masked) continue;
                const int x = sx + cx;
                if (x < 0 || x >= w) continue;
                const int col = hf ? wc * 8 - 1 - cx : cx;
                const int tile = ((attr & 0x7FF) + (col >> 3) * hc + (row >> 3)) & 0x7FF;
                const uint8_t b = vram[(tile * 32 + (row & 7) * 4 + ((col & 7) >> 1)) & 0xFFFF];
                const uint8_t p = (col & 1) ? (b & 15) : (b >> 4);
                if (p && !(buf[x].color & 15)) {   // first sprite in the list wins
                    buf[x].color = uint8_t(pal | p);
                    buf[x].pri = pri;
                }
            }
            if (linePixels <= 0) break;
        }
        link = next;
        if (link == 0 || link >= maxSprites) break;
    }
}

void Vdp::renderLine(int y, uint16_t* out) {
    const int w = width();
    const uint16_t backdrop = cram[reg[7] & 0x3F];
    if (!(reg[1] & 0x40)) {   // display disabled
        std::fill(out, out + w, backdrop);
        return;
    }
    Px a[kMaxWidth], b[kMaxWidth], s[kMaxWidth];
    std::memset(s, 0, sizeof s);
    planeLine(y, false, b, w);
    planeLine(y, true, a, w);
    int wx0, wx1;
    if (windowSpan(y, w, wx0, wx1)) windowLine(y, a, w, wx0, wx1);
    spriteLine(y, s, w);

    for (int x = 0; x < w; x++) {
        // Lowest to highest: backdrop, B low, A low, sprite low, B high, A high, sprite high.
        uint8_t c = reg[7] & 0x3F;
        bool opaque = false;
        auto put = [&](const Px& p, bool wantPri) {
            if ((p.color & 15) && bool(p.pri) == wantPri) { c = p.color; opaque = true; }
        };
        put(b[x], false); put(a[x], false); put(s[x], false);
        put(b[x], true); put(a[x], true); put(s[x], true);
        out[x] = opaque ? cram[c & 63] : backdrop;
    }
    if (reg[0] & 0x20) std::fill(out, out + 8, backdrop);   // left column blanking
}

void Vdp::renderFrame(std::vector<uint16_t>& out, const std::function<void(int)>& onLine) {
    out.assign(size_t(kMaxWidth) * kHeight, 0);
    for (int y = 0; y < kHeight; y++) {
        if (onLine) onLine(y);
        renderLine(y, &out[size_t(y) * kMaxWidth]);
    }
}
