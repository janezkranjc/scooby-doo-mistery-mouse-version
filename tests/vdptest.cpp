// Renders a reference VDP dump captured from the emulator and compares the
// result with the emulator's own frame. Usage: vdptest dump.bin [--split]
//
// --split reproduces the game's mid-frame change: lines above 168 use the
// room planes and scroll values the game keeps in work RAM, lines from 168
// use the registers as dumped at end of frame.
#include "../src/core/vdp.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: vdptest dump.bin [--split] [--ppm out.ppm]\n"); return 2; }
    bool split = false; std::string ppm;
    for (int i = 2; i < argc; i++) {
        if (!std::strcmp(argv[i], "--split")) split = true;
        else if (!std::strcmp(argv[i], "--ppm") && i + 1 < argc) ppm = argv[++i];
    }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    const size_t need = 32 + 128 + 80 + 0x10000 + 0x10000 + 4;
    if (d.size() < need) { std::fprintf(stderr, "short dump\n"); return 2; }

    Vdp vdp;
    const uint8_t* p = d.data();
    std::memcpy(vdp.reg.data(), p, 32); p += 32;
    for (int i = 0; i < 64; i++) vdp.cram[i] = be16(p + 2 * i);
    p += 128;
    for (int i = 0; i < 40; i++) vdp.vsram[i] = be16(p + 2 * i);
    p += 80;
    std::memcpy(vdp.vram.data(), p, 0x10000); p += 0x10000;
    const uint8_t* ram = p; p += 0x10000;
    const int rw = be16(p), rh = be16(p + 2); p += 4;
    const uint8_t* ref = p;

    // End-of-frame values, restored at the split line.
    const uint8_t r2 = vdp.reg[2], r4 = vdp.reg[4], r18 = vdp.reg[18];
    const uint16_t vsA = vdp.vsram[0], vsB = vdp.vsram[1];
    const uint16_t hsTab = uint16_t((vdp.reg[13] & 0x3F) << 10);
    const uint16_t hsA = vdp.vramWord(hsTab), hsB = vdp.vramWord(uint16_t(hsTab + 2));

    std::vector<uint16_t> out;
    vdp.renderFrame(out, [&](int y) {
        if (!split) return;
        if (y == 0) {
            vdp.reg[2] = 0x30; vdp.reg[4] = 0x07;
            vdp.vramPoke(hsTab, be16(ram + 0x07F8)); vdp.vramPoke(uint16_t(hsTab + 2), be16(ram + 0x07FA));
            vdp.vsram[0] = be16(ram + 0x07FC); vdp.vsram[1] = be16(ram + 0x07FE);
        } else if (y == 168) {
            vdp.reg[2] = r2; vdp.reg[4] = r4; vdp.reg[18] = r18;
            vdp.vramPoke(hsTab, hsA); vdp.vramPoke(uint16_t(hsTab + 2), hsB);
            vdp.vsram[0] = vsA; vdp.vsram[1] = vsB;
        }
    });

    const int w = vdp.width();
    if (w != rw || rh != Vdp::kHeight) { std::printf("size mismatch: mine %dx224 ref %dx%d\n", w, rw, rh); return 1; }
    long bad = 0; int firstX = -1, firstY = -1; std::vector<int> badLines(224, 0);
    for (int y = 0; y < 224; y++)
        for (int x = 0; x < w; x++) {
            const uint16_t c = out[size_t(y) * Vdp::kMaxWidth + x];
            const uint8_t* q = ref + (size_t(y) * w + x) * 3;
            if (((c >> 1) & 7) != q[0] || ((c >> 5) & 7) != q[1] || ((c >> 9) & 7) != q[2]) {
                if (!bad) { firstX = x; firstY = y; }
                bad++; badLines[y]++;
            }
        }
    if (!ppm.empty()) {
        std::ofstream o(ppm, std::ios::binary);
        o << "P6\n" << w << " 224\n255\n";
        for (int y = 0; y < 224; y++)
            for (int x = 0; x < w; x++) {
                uint32_t c = Vdp::toRgba(out[size_t(y) * Vdp::kMaxWidth + x]);
                char px[3] = {char(c), char(c >> 8), char(c >> 16)};
                o.write(px, 3);
            }
    }
    std::printf("%s: %dx224, mismatched pixels %ld", argv[1], w, bad);
    if (bad) {
        std::printf(" (first at %d,%d; lines:", firstX, firstY);
        int shown = 0;
        for (int y = 0; y < 224 && shown < 12; y++) if (badLines[y]) { std::printf(" %d:%d", y, badLines[y]); shown++; }
        std::printf(")");
    }
    std::printf("\n");
    return bad ? 1 : 0;
}
