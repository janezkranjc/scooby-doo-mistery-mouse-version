// Software model of the Mega Drive video chip, limited to what this game uses:
// mode 5, two scrolling planes, the window plane, sprites and 64-colour CRAM.
//
// The game's data is authored for this hardware (4bpp tiles, tile maps,
// sprite pieces), so the reimplementation keeps a faithful compositor and
// draws it line by line. A per-line hook reproduces the game's mid-frame
// split between the room view and the verb panel.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

class Vdp {
public:
    static constexpr int kMaxWidth = 320;
    static constexpr int kHeight = 224;

    std::array<uint8_t, 0x10000> vram{};
    std::array<uint16_t, 64> cram{};   // 0000BBB0GGG0RRR0
    std::array<uint16_t, 40> vsram{};
    std::array<uint8_t, 32> reg{};

    // Lifts the hardware's per-line sprite limits (an enhancement, off by default).
    bool unlimitedSprites = false;

    void reset();

    // --- register / memory access, named after what the game code does ---
    void setReg(int r, uint8_t v) { reg[r & 31] = v; }
    void regWord(uint16_t w) { setReg((w >> 8) & 0x1F, uint8_t(w)); }   // 0x8RVV form
    uint16_t vramWord(uint16_t a) const { return uint16_t(vram[a] << 8 | vram[uint16_t(a + 1)]); }
    void vramPoke(uint16_t a, uint16_t v) { vram[a] = uint8_t(v >> 8); vram[uint16_t(a + 1)] = uint8_t(v); }
    void vramWrite(uint16_t a, const uint8_t* src, int words, int step = 2);
    void vramFill(uint16_t a, uint16_t v, int words);
    void cramPoke(int index, uint16_t v) { cram[index & 63] = v & 0x0EEE; }

    int width() const { return (reg[12] & 0x01) ? 320 : 256; }

    // Renders one frame of 9-bit colour (0000BBB0GGG0RRR0 per pixel) into `out`
    // (width()*224 entries, width sampled per line into lineWidth).
    // `onLine(y)` runs before line y is drawn and may change registers/memory.
    void renderFrame(std::vector<uint16_t>& out, const std::function<void(int)>& onLine = {});
    void renderLine(int y, uint16_t* out);

    static uint32_t toRgba(uint16_t c9);

private:
    struct Px { uint8_t color; uint8_t pri; };   // color = palette index 0..63, 0 in low nibble = transparent
    void planeLine(int y, bool planeA, Px* buf, int w) const;
    void windowLine(int y, Px* buf, int w, int x0, int x1) const;
    void spriteLine(int y, Px* buf, int w) const;
    bool windowSpan(int y, int w, int& x0, int& x1) const;
};
