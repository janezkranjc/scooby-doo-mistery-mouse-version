#include "menu.h"
#include "addr.h"
#include "interrupts.h"
#include "lowlevel.h"
#include "scaler.h"
#include "sound.h"

namespace menu {

namespace {

constexpr u32 kLetters = 0x871E;        // 32 password symbols
constexpr u32 kLogoSprites = 0x85F8;    // 12 sprites and a terminator
constexpr u32 kPwBytes = 0xFF001E;      // room, checksum, then 29 flag bytes
constexpr u32 kPwSymbols = 0xFF003E;    // 50 five-bit values
constexpr u32 kTextBuf = 0xFF08BC;
constexpr u32 kMenuTimer = 0xFF08AE;    // counted down by the frame interrupt

bool pressed(int bit) { return !BTST(PadState, bit); }

// Additions for mouse play; the original menus are pad only. Call once per
// frame: `click` and `back` are set on the frame a button goes down, and
// `moved` when the pointer has moved since the last call.
struct MenuMouse {
    bool click = false, back = false, middle = false, moved = false;
    int x = 0, y = 0, wheel = 0;
};
MenuMouse pollMouse() {
    static bool left = false, right = false, mid = false;
    MenuMouse m;
    if (!M.mouseEnabled) { M.mouseWheel = 0; return m; }
    m.x = M.mouseX; m.y = M.mouseY;
    m.click = M.mouseLeft && !left;
    m.back = M.mouseRight && !right;
    m.middle = M.mouseMiddle && !mid;
    left = M.mouseLeft; right = M.mouseRight; mid = M.mouseMiddle;
    m.moved = M.mouseMoved;
    M.mouseMoved = false;
    m.wheel = M.mouseWheel;
    M.mouseWheel = 0;
    return m;
}

// Which option line of a menu list is at this point, or -1. Lines are 24
// pixels apart, the first one starting at y 88.
int optionAt(const MenuMouse& m, u16 count) {
    if (m.x < 40 || m.x >= 216 || m.y < 84) return -1;
    const int i = (m.y - 84) / 24;
    return i < count ? i : -1;
}

void waitMenuTimer() {
    while (RS16(kMenuTimer) >= 0) ll::idleFrame();
}

void scaleLogo() {
    scaler::titleLogoFront();
    scaler::titleLogoBack();
}

// 0x8F40: rewrites the Y word of the twelve logo sprites so the logo bobs.
void bobLogo() {
    const u16 dy = R8(0xFF001D) >> 3;
    for (int i = 0; i < 12; i++) ll::vramPoke(u16(0xFC00 + i * 8), u16(R16(kLogoSprites + i * 8) + dy));
}

// 0x8F1C: uploads both scaled logo layers, then bobs the sprites.
void uploadLogo() {
    ll::dmaVram(0xFFE000, 0xF000, 0x4B0);
    ll::dmaVram(0xFFC400, 0xC800, 0x200);
    bobLogo();
}

// The original keeps the previous pad byte in 0xFF0010 and also latches
// changes the interrupt saw between polls, so a quick tap is not lost.
bool tapped(int bit) {
    if (!pressed(bit)) return false;
    if (!BTST(0xFF0010, bit) && !BTST(PadEdge, bit)) return false;
    W8(PadEdge, 0);
    W8(PadLatch, R8(PadState));
    return true;
}

// 0x8D6A. Runs the option cursor until Start is pressed. Returns the index.
u16 selectOption(u16 count) {
    W16(0xFF08BA, 0);
    u16 frame = 0, y = 0;
    s16 hold = 1;
    const u16 maxY = u16((count - 1) * 0x18);
    W8(0xFF0010, R8(PadPrev));
    bool rearm = false;
    for (;;) {
        if (rearm) W16(kMenuTimer, 4);
        rearm = true;
        scaleLogo();
        for (;;) {
            // Hidden button sequence that unlocks the extended pause menu.
            if (!BTST(0xFF09ED, 1)) {
                const u16 i = R16(0xFF08BA);
                const u8 pad = R8(PadState);
                if (u8(~R8(0x10118 + i)) == pad) {
                    W16(0xFF08BA, u16(i + 1));
                    if (R16(0xFF08BA) == 0x0B) {
                        BSET(0xFF09ED, 1);
                        snd::startSequence(3);
                    }
                } else if (!(i != 0 && u8(~R8(0x10118 + i - 1)) == pad) && pad != 0xFF) {
                    W16(0xFF08BA, 0);
                }
            }
            if (pressed(0) && BTST(0xFF0010, 0) && y != 0) y = u16(y - 0x18);
            if (pressed(1) && BTST(0xFF0010, 1) && y != maxY) y = u16(y + 0x18);
            // Mouse: pointing at a line moves the cursor to it, a click picks it.
            const MenuMouse mouse = pollMouse();
            const int over = optionAt(mouse, count);
            if (over >= 0 && (mouse.moved || mouse.click)) y = u16(over * 0x18);
            if ((pressed(7) && BTST(0xFF0010, 7)) || (mouse.click && over >= 0)) {   // Start newly pressed
                waitMenuTimer();
                uploadLogo();
                W16(kMenuTimer, 4);
                ll::vramPoke(0xFC60, 0);
                ll::vramPoke(0xFC62, 0);
                return u16(y / 0x18);
            }
            W8(0xFF0010, R8(PadState));
            ll::waitVBlank();
            if (RS16(kMenuTimer) < 0) break;
        }
        uploadLogo();
        ll::vramPoke(0xFC60, u16(0xD0 + y));
        ll::vramPoke(0xFC62, 0x0F00);
        ll::vramPoke(0xFC64, u16(R16(0xFF0008) + R16(0x8F14 + frame)));
        ll::vramPoke(0xFC66, 0x00A8);
        if (--hold < 0) {
            hold = 1;
            frame = (frame + 2) & 7;
        }
    }
}

// 0x8D36: shows an option list and runs the cursor on it.
u16 submenu(u32 packedMap, u16 count) {
    ll::packbitsDecode(packedMap, 0xFF3000);
    waitMenuTimer();
    W16(kMenuTimer, 4);
    uploadLogo();
    ll::dmaVram(0xFF3000, 0xC000, 0x380);
    return selectOption(count);
}

// 0x8CD6: leaves the menu and restores the in-game video setup.
void leaveToGame() {
    ll::fadeOut();
    M.vdp.regWord(0x9001);
    BSET(0xFF09ED, 0);
    if (R16(0xFF06AC) != R16(0xFF000A)) W16(0xFF069E, 0);
    W16(0xFF06AC, R16(0xFF000A));
    M.vdp.regWord(0x856C);
    BCLR(0xFF09EB, 1);
    W32(HBlankHandler, irq::HblSplit);
    W32(VBlankHandler, irq::VblMain);
}

// 0x7F22: resets the story flags to the chosen episode's defaults.
void resetEpisodeFlags() {
    const u32 hdr = R32(0x31AF2 + R16(EpisodeIndex) * 4);
    for (int i = 0; i < 0x80; i++) W16(0xFF2A00 + 2 * i, 0);
    u32 dst = 0xFF2A00;
    for (u32 p = R32(hdr); p != R32(hdr + 4); p++) W8(dst++, R8(p));
    W16(0xFF06AC, R16(R32(hdr + 0x30)));
}

// 0x873E
void scramblePassword() {
    u8 key = u8((R8(kPwBytes + 1) ^ R8(kPwBytes)) & 0x1F);
    W8(kPwBytes, key);
    for (int i = 0; i < 0x1D; i++) {
        key ^= R8(kPwBytes + 2 + i);
        W8(kPwBytes + 2 + i, key);
    }
}

// 0x876C
void unscramblePassword() {
    u8 prev = R8(kPwBytes);
    W8(kPwBytes, u8((R8(kPwBytes + 1) ^ prev) & 0x1F));
    for (int i = 0; i < 0x1D; i++) {
        const u8 b = R8(kPwBytes + 2 + i);
        W8(kPwBytes + 2 + i, u8(b ^ prev));
        prev = b;
    }
}

// The checksum is a chain of rotate-left-through-carry and XOR over the 29
// flag bytes, seeded with the room number (0x8674-0x8692, 0x92F2-0x930E).
u8 passwordChecksum(u8 seed) {
    bool x = false;
    auto roxl = [&](u8 v) {
        const bool out = v & 0x80;
        v = u8(v << 1 | (x ? 1 : 0));
        x = out;
        return v;
    };
    u8 d0 = roxl(roxl(seed));
    for (int i = 0; i < 0x1D; i++) d0 = u8(roxl(d0) ^ R8(kPwBytes + 2 + i));
    return d0;
}

// 0x9298: checks the entered password. On success the flags and room are
// installed and true is returned.
bool acceptPassword() {
    u32 text = kTextBuf, sym = kPwSymbols;
    for (int line = 0; line < 2; line++) {
        for (int group = 0; group < 5; group++) {
            for (int i = 0; i < 5; i++) {
                const u8 ch = R8(text++);
                u8 v = 0;
                while (R8(kLetters + v) != ch) v++;
                W8(sym++, v);
            }
            text++;   // space
        }
        text++;       // terminator
    }
    // Repack 5-bit symbols into bytes, most significant bit first.
    u32 bitPos = 0;
    for (int i = 0; i < 32; i++) {
        u8 b = 0;
        for (int k = 0; k < 8; k++, bitPos++) {
            const u8 v = R8(kPwSymbols + bitPos / 5);
            b = u8(b << 1 | ((v >> (4 - bitPos % 5)) & 1));
        }
        W8(kPwBytes + i, b);
    }
    unscramblePassword();
    if (passwordChecksum(R8(kPwBytes)) != R8(kPwBytes + 1)) {
        snd::startSequence(5);
        return false;
    }
    snd::startSequence(0x59);
    ll::waitFrames(0x3C);
    BSET(0xFF09ED, 2);
    for (int i = 0; i < 0x80; i++) W16(0xFF2A00 + 2 * i, 0);
    for (int i = 0; i < 0x1D; i++) W8(0xFF2A00 + i, R8(kPwBytes + 2 + i));
    W16(0xFF0A90, R8(kPwBytes));
    return true;
}

// 0x8FF8-0x9276: password entry. Returns true to start the game, false to
// go back to the main options.
bool passwordScreen() {
    ll::packbitsDecode(0x18E48, 0xFF3000);
    waitMenuTimer();
    W16(kMenuTimer, 4);
    uploadLogo();
    makePassword();
    ll::dmaVram(0xFF3000, 0xC000, 0x380);
    u16 col = 0, row = 0, line = 0, caret = 0;
    for (;;) {
        W16(kMenuTimer, 4);
        scaleLogo();
        for (;;) {
            if (tapped(0)) row = row ? u16(row - 1) : 4;
            if (tapped(1)) row = row + 1 < 5 ? u16(row + 1) : 0;
            if (tapped(3)) col = (col + 1) & 7;
            if (tapped(2)) col = (col - 1) & 7;
            // Mouse: click a letter or a command to use it, click in the
            // password text to move the caret there, right click to go back.
            const MenuMouse mouse = pollMouse();
            bool act = false;
            if (mouse.click) {
                if (mouse.x >= 88 && mouse.x < 152 && mouse.y >= 112 && mouse.y < 192) {
                    col = u16((mouse.x - 88) / 8);
                    row = u16((mouse.y - 112) / 16);
                    act = true;
                } else if (mouse.x >= 8 && mouse.x < 240 && mouse.y >= 56 && mouse.y < 72) {
                    const int pos = (mouse.x - 8) / 8;
                    if (pos % 6 != 5) { line = u16((mouse.y - 56) / 8); caret = u16(pos - pos / 6); }
                }
            }
            if (mouse.back) { row = 4; col = 7; act = true; }
            // A, B or Start newly pressed
            if (act || ((R8(0xFF0010) & 0xD0) == 0xD0 && (R8(PadState) & 0xD0) != 0xD0)) {
                if (row == 4) {
                    switch (col) {
                        case 0: snd::startSequence(0x75); caret = caret ? u16(caret - 1) : 0x18; break;
                        case 1: snd::startSequence(0x75); caret = caret < 0x18 ? u16(caret + 1) : 0; break;
                        case 2:
                        case 3: snd::startSequence(0x75); line ^= 1; break;
                        case 4: snd::startSequence(0x63); makePassword(); caret = 0; line = 0; break;
                        case 5: if (acceptPassword()) return true; break;
                        case 6: break;
                        case 7:
                            ll::waitVBlank();
                            ll::vramPoke(0xFC60, 0);
                            ll::vramPoke(0xFC62, 0);
                            return false;
                    }
                } else {
                    snd::startSequence(0x27);
                    const u8 ch = R8(kLetters + row * 8 + col);
                    W8(kTextBuf + (line ? 0x1F : 0) + caret + caret / 5, ch);
                    if (++caret > 0x18) {
                        caret = 0;
                        line ^= 1;
                    }
                }
            }
            W8(0xFF0010, R8(PadState));
            ll::waitVBlank();
            if (RS16(kMenuTimer) < 0) break;
        }
        uploadLogo();
        // Two sprites: the letter-grid cursor and the caret under the text.
        ll::vramPoke(0xFC60, u16(row * 16 + 0xF0));
        ll::vramPoke(0xFC62, 0x010D);
        ll::vramPoke(0xFC64, 0x07D0);
        ll::vramPoke(0xFC66, u16(col * 8 + 0xD8));
        ll::vramPoke(0xFC68, u16(line * 8 + 0xB8));
        ll::vramPoke(0xFC6A, 0x0000);
        ll::vramPoke(0xFC6C, 0x07D0);
        ll::vramPoke(0xFC6E, u16((caret + caret / 5) * 8 + 0x88));
        const u32 next = ll::drawStringHigh(kTextBuf, 1, 7);
        ll::drawStringHigh(next, 1, 8);
    }
}

// 0x940C: sound test. Up and down change the first digit of the number,
// left and right the second, B plays, C stops, A leaves. The name of the
// tune is shown under the number. Mouse additions: the wheel steps through
// the numbers, left click plays, middle click stops, right click leaves.
void soundTestScreen() {
    constexpr u32 kNames = 0x1F7082, kIds = 0x1F7492, kHex = 0x93FC;
    W8(0xFF0010, R8(PadPrev));
    scaleLogo();
    snd::stopAll();
    ll::packbitsDecode(0x18C72, 0xFF3000);
    s16 hi = 0, lo = 0;
    u16 shown = 0xFFFF;
    BSET(TextAttrBase, 7);
    waitMenuTimer();
    W16(kMenuTimer, 4);
    ll::dmaVram(0xFF3000, 0xC000, 0x380);
    uploadLogo();
    for (;;) {
        scaleLogo();
        for (;;) {
            const MenuMouse mouse = pollMouse();
            if (tapped(0)) hi++;
            if (tapped(1)) hi--;
            if (tapped(3)) lo++;
            if (tapped(2)) lo--;
            for (int n = mouse.wheel; n != 0; n += n > 0 ? -1 : 1) {   // one tune per notch, wrapping at the ends
                int v = hi * 16 + lo + (n > 0 ? 1 : -1);
                if (v > 0x56) v = 0;
                if (v < 0) v = 0x56;
                hi = s16(v >> 4); lo = s16(v & 15);
            }
            if (hi == 5) {
                if (lo < 0) { hi--; lo &= 0x0F; }
                else if (lo == 7) { hi = 0; lo = 0; }
            } else if (lo < 0) { hi--; lo &= 0x0F; }
            else if (lo == 0x10) { lo &= 0x0F; hi++; }
            if (hi < 0) hi = 5;
            else if (hi > 5) hi = 0;
            if (hi == 5 && lo >= 7) lo = 6;
            if (tapped(4) || mouse.click) {
                snd::stopAll();
                if (shown != 0xFFFF) snd::startSequence(R8(kIds + shown));
            }
            if (tapped(6) || mouse.middle) snd::stopAll();
            if (mouse.back || (pressed(5) && (BTST(0xFF0010, 5) || BTST(PadEdge, 5)))) {
                waitMenuTimer();
                uploadLogo();
                BCLR(TextAttrBase, 7);
                snd::stopAll();
                snd::startSequence(R32(0xFF06A6));
                return;
            }
            W8(0xFF0010, R8(PadState));
            if (RS16(kMenuTimer) < 0) break;
            ll::idleFrame();   // the original spins here until the timer runs out
        }
        W16(kMenuTimer, 4);
        uploadLogo();
        const u16 index = u16(hi << 4 | lo);
        if (index != shown) {
            shown = index;
            u32 name = kNames;
            for (u16 n = index; n > 0; n--)
                while (R8(name++)) {}
            ll::waitVBlank();
            ll::drawString(0x2FFF4, 1, 0x11);
            u16 len = 0;
            while (R8(name + len)) len++;
            ll::drawString(name, u16((0x20 - len) >> 1), 0x11);
        }
        ll::vramPoke(0xC51E, u16(R8(kHex + hi) + R16(TextAttrBase)));
        ll::vramPoke(0xC520, u16(R8(kHex + lo) + R16(TextAttrBase)));
    }
}

// 0x8FB8 / 0x8FC2: episode chosen. Returns true when a game should start.
bool episodeMenu(u16 episode) {
    W16(EpisodeIndex, episode);
    resetEpisodeFlags();
    const u16 sel = submenu(episode ? 0x18B3C : 0x18A56, 3);
    if (sel == 0) return true;
    if (sel == 2) return false;
    return passwordScreen();
}

}  // namespace

// 0x8660. Builds the password for the current room and story flags.
void makePassword() {
    const u8 room = u8(R16(0xFF06AC));
    W8(kPwBytes, room);
    W8(kPwBytes + 1, 0);
    for (int i = 0; i < 0x1D; i++) W8(kPwBytes + 2 + i, R8(0xFF2A00 + i));
    W8(kPwBytes + 1, passwordChecksum(room));
    scramblePassword();
    // 50 symbols of five bits each, read from the byte stream MSB first.
    for (u32 n = 0; n < 50; n++) {
        u8 v = 0;
        for (u32 k = 0; k < 5; k++) {
            const u32 bit = n * 5 + k;
            v = u8(v << 1 | ((R8(kPwBytes + bit / 8) >> (7 - bit % 8)) & 1));
        }
        W8(kPwSymbols + n, v);
    }
    u32 out = kTextBuf, sym = kPwSymbols;
    for (int line = 0; line < 2; line++) {
        for (int group = 0; group < 5; group++) {
            for (int i = 0; i < 5; i++) W8(out++, R8(kLetters + R8(sym++)));
            W8(out++, ' ');
        }
        W8(out++, 0);
    }
}

// 0x879E
void titleFrameWork() {
    if (BTST(0xFF09ED, 4)) W8(0xFF0627, R8(0xFF001D));
    if (R8(0xFF001C) == 0) {
        W8(0xFF001D, u8(R8(0xFF001D) + 2));
        if (R8(0xFF001D) == 0x40) W8(0xFF001C, 1);
    } else {
        W8(0xFF001D, u8(R8(0xFF001D) - 2));
        if (R8(0xFF001D) == 0) W8(0xFF001C, 0);
    }
    W16(0xFF000E, u16(R16(0xFF000E) - 1));
    if (RS16(0xFF000E) < 0) {
        W16(0xFF000E, 3);
        for (int i = 0; i < 0x3F; i++) W16(0xFF8C00 + 2 * i, R16(0xFF8C02 + 2 * i));
        u16 i = R16(0xFF000C);
        W16(0xFF8C7E, R16(0x8828 + i));
        i = u16(i + 2);
        if (s16(i) >= 0x4E) i = 0;
        W16(0xFF000C, i);
    }
}

// 0xA38C-0xA49C. At random intervals a patch of the backdrop map is swapped
// for a lightning bolt while palette line 2 steps through three flash
// palettes, then the patch is restored.
void titleFlash() {
    if (!BTST(0xFF09EB, 7)) return;
    if (RS16(0xFF0016) < 0) {
        W16(0xFF001A, u16(R16(0xFF001A) - 1));
        if (RS16(0xFF001A) >= 0) return;
        W16(0xFF001A, 2);
        ll::dmaCram(R32(0xA4E8 + R16(0xFF0018)), 0x40, 0x10);
        W16(0xFF0018, u16(R16(0xFF0018) - 4));
        if (RS16(0xFF0018) >= 0) return;
        const u16 off = R16(0xFF0810);
        u32 src = 0x165B8 + off;
        u16 addr = u16(off + 0xE000);
        const u16 w = R16(0xFF0812), h = R16(0xFF0814);
        for (u32 r = 0; r <= h; r++) {
            src = ll::dmaVram(src, addr, w);
            addr = u16(addr + 0x40);
            src += s16(0x40 - 2 * w);
        }
        W16(0xFF0016, ll::random(0xC8));
    } else {
        W16(0xFF0016, u16(R16(0xFF0016) - 1));
        if (RS16(0xFF0016) >= 0) return;
        W16(0xFF0018, 0x0C);
        W16(0xFF001A, 2);
        ll::dmaCram(0xA558, 0x40, 0x10);
        const u16 pick = u16(ll::random(3) << 2);
        u32 src = R32(0xA578 + pick);
        const u16 off = u16((R16(0xA578 + 0x0E + pick) << 6) + R16(0xA578 + 0x0C + pick) * 2);
        W16(0xFF0810, off);
        u16 addr = u16(off + 0xE000);
        const u16 w = R16(0xA578 + 0x18 + pick), h = R16(0xA578 + 0x1A + pick);
        W16(0xFF0812, w);
        W16(0xFF0814, h);
        for (u32 r = 0; r <= h; r++) {
            src = ll::dmaVram(src, addr, w);
            addr = u16(addr + 0x40);
        }
    }
}

void titleMenu() {
    ll::fadeOut();
    BCLR(0xFF09ED, 2);
    BCLR(0xFF09ED, 4);
    BCLR(0xFF09EB, 7);
    M.vdp.regWord(0x9100);
    M.vdp.regWord(0x9200);
    W16(0xFF0016, 0);
    W16(0xFF001C, 0);
    W16(0xFF001A, 0);
    W16(0xFF000A, R16(0xFF06AC));
    W16(0xFF0010, 0xE0);
    W16(0xFF0012, 0xE0);
    ll::vramPoke(0xDC00, 0);
    ll::vramPoke(0xDC02, 0);
    M.vdp.vsram[0] = 0;
    M.vdp.vsram[1] = 0;
    W16(0xFF000C, 0);
    W16(0xFF000E, 3);
    BCLR(0xFF09EB, 3);
    BSET(0xFF09EB, 4);
    ll::vramClear(0xDC00, 0x1C0);
    ll::vramClear(0, 0x8000);
    BSET(0xFF09EB, 1);
    M.vdp.regWord(0x857E);
    ll::vramPoke(0xFC00, 0);
    ll::vramPoke(0xFC02, 0);
    W32(VBlankHandler, irq::VblTitle);
    W32(HBlankHandler, irq::HblNone);
    M.vdp.regWord(0x9100);
    M.vdp.regWord(0x9200);
    M.vdp.regWord(0x9000);

    // Backdrop tiles, then three small tile sets placed right after them.
    ll::lzssDecode(0x1155A, 0xFF3000, 0xFF4000);
    const u16 words = u16(R32(0xFF3000)) >> 1;
    ll::vramWrite(0, words, 0xFF4000);
    W16(0xFF0008, words >> 4);            // first tile after the backdrop set
    W16(TextAttrBase, 0x680);
    u16 addr = u16(words * 2);
    for (u32 src : {0x16D38u, 0x16E6Au, 0x16F76u}) {
        ll::packbitsDecode(src, 0xFF3000);
        ll::vramWrite(addr, 0x100, 0xFF3000);
        addr = u16(addr + 0x200);
    }
    ll::loadFont(0xF, 0, R16(TextAttrBase));
    ll::vramFill(0xFA00, 0x40, 0xAAAA);
    ll::vramWrite(0xE000, 0x380, 0x165B8);

    // The two logo layers, one pixel per byte, ready for the scalers.
    const u32 logo = 0x10136;
    const u16 list = R16(logo + 4);
    ll::packbitsDecode(logo + s16(u16(R32(logo + R16(logo + list) + 6))), 0xFF3800);
    ll::packbitsDecode(logo + s16(u16(R32(logo + R16(logo + list + 2) + 6))), 0xFF5000);
    W32(0xFF37FC, 0);
    W32(0xFF5800, 0);
    ll::vramWrite(0xFC00, 0x34, kLogoSprites);
    for (int i = 0; i < 64; i++) W16(TargetPalette + 2 * i, R16(0x16CB8 + 2 * i));
    for (int i = 0; i < 30; i++) W16(TargetPalette + 2 + 2 * i, R16(0x32482 + 2 * i));
    makePassword();
    ll::fadeIn(TargetPalette);
    BSET(0xFF09EB, 7);
    W16(0xFF0624, 0xE0);
    W16(0xFF0626, 0xE0);

    // Opening: the logo grows in over a 28-step backdrop animation. Any
    // button skips it.
    u32 frames = 0x17064;
    ll::waitVBlank();
    W16(kMenuTimer, 4);
    bool skipped = false;
    for (int step = 0x1B; step >= 0 && !skipped; step--) {
        if (step == 0x1B || step == 0x0D) snd::startSequence(0x0B);
        ll::packbitsDecode(R32(frames), 0xFF3000);
        frames += 4;
        if (!BTST(0xFF09ED, 4)) {
            W16(0xFF0624, R16(0xFF0010));
            W16(0xFF0626, R16(0xFF0010));
            if (R16(0xFF0010) != 0) W16(0xFF0010, u16(R16(0xFF0010) - 0x20));
        }
        scaler::titleLogoFront();
        if (R16(0xFF0010) == 0) {
            if (!BTST(0xFF09ED, 4)) {
                W16(0xFF0624, R16(0xFF0012));
                W16(0xFF0626, R16(0xFF0012));
                if (R16(0xFF0012) == 0) BSET(0xFF09ED, 4);
                else W16(0xFF0012, u16(R16(0xFF0012) - 0x20));
            }
            scaler::titleLogoBack();
        }
        for (;;) {
            ll::waitVBlank();
            if ((R8(PadState) & 0xF0) != 0xF0 || pollMouse().click) { skipped = true; break; }
            if (RS16(kMenuTimer) < 0) break;
        }
        if (skipped) break;
        ll::dmaVram(0xFFE000, 0xF000, 0x4B0);
        if (R16(0xFF0010) == 0) ll::dmaVram(0xFFC400, 0xC800, 0x200);
        bobLogo();
        ll::dmaVram(0xFF3000, 0xC000, 0x380);
        W16(kMenuTimer, 4);
    }

    for (;;) {   // 0x851E
        W16(0xFF0624, 0);
        W16(0xFF0626, 0);
        BSET(0xFF09ED, 4);
        scaleLogo();
        const bool paused = BTST(0xFF09ED, 0);
        const bool extended = BTST(0xFF09ED, 1);
        const u32 map = paused ? (extended ? 0x18CBE : 0x18BFA) : 0x1896C;
        const u16 count = paused ? (extended ? 4 : 2) : 3;
        ll::packbitsDecode(map, 0xFF3000);
        waitMenuTimer();
        W16(kMenuTimer, 4);
        uploadLogo();
        ll::dmaVram(0xFF3000, 0xC000, 0x380);
        if (paused) {
            const u32 next = ll::drawString(kTextBuf, 1, 7);
            ll::drawString(next, 1, 8);
        }
        const u16 sel = selectOption(count);
        if (!paused) {
            if (sel <= 1) {
                if (episodeMenu(sel)) { leaveToGame(); return; }
            } else {
                soundTestScreen();
            }
        } else {
            if (sel == 0) { leaveToGame(); return; }
            if (sel == 1) {   // 0x8F8A: quit, with a confirmation list
                if (submenu(0x18F58, 4) != 0) {
                    BCLR(EngineFlags, 6);
                    ll::fadeOut();
                    throw Restart{};
                }
            } else {
                extern void debugMenu(u16 which);
                debugMenu(sel);
            }
        }
    }
}

}  // namespace menu
