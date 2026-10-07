#include "ui.h"
#include "actors.h"
#include "addr.h"
#include "autoplay.h"
#include "lowlevel.h"
#include "script.h"
#include <vector>

namespace ui {

namespace {

constexpr u32 kText = 0xFF08BC;

u32 strCopy(u32 src, u32 dst) {   // 0x5B56: returns the address after the copied terminator
    for (;;) {
        const u8 c = R8(src++);
        W8(dst++, c);
        if (!c) return dst;
    }
}

void pokeChars(u32& text, u16 addr, int count) {
    for (int i = 0; i < count; i++, addr = u16(addr + 2)) ll::vramPoke(addr, u16(R8(text++) + R16(TextAttrBase)));
}

// Length of the next line: at most 30 characters, broken after a space or hyphen.
int lineBreak(u32 text) {
    int back = 0;
    for (u32 p = text + 0x1E;; p--, back++)
        if (R8(p) == ' ' || R8(p + 1) == '-') break;
    return 0x1E - back;
}

void clearPanelText(u16 addr) {   // 0x22F2
    const u16 blank = u16(R16(TextAttrBase) + 0x20);
    for (int r = 0; r < 6; r++, addr = u16(addr + 0x40))
        for (int c = 0; c < 0x20; c++, addr = u16(addr + 2)) ll::vramPoke(addr, blank);
}

u16 panelTextAddr() { return u16((BTST(EngineFlags, 4) ? 0 : 0x40) + 0xAA80); }

void savePanelRows(u16 addr) {
    u32 buf = 0xFF2C00;
    for (int r = 0; r < 6; r++, addr = u16(addr + 0x40))
        for (int c = 0; c < 0x20; c++, addr = u16(addr + 2), buf += 2) W16(buf, ll::vramPeek(addr));
}

void restorePanelRows(u16 addr) {
    u32 buf = 0xFF2C00;
    for (int r = 0; r < 6; r++, addr = u16(addr + 0x80)) buf = ll::dmaVram(buf, addr, 0x20);
}

void slidePanel() {   // start the 16-frame slide and wait for it
    W8(0xFF0ACB, 0x10);
    actor::spin([] { return RS8(0xFF0ACB) < 0; });
}

// 0x9FA2: wrapped text in the panel. Returns the number of lines.
u16 drawPanelText(u16 row, u16 col, u32 text) {
    int len = ll::strLen(text);
    u16 addr = u16(0xAA82 + (row << 7) + col * 2);
    u16 lines = 1;
    while (len > 0x1E) {
        const int n = lineBreak(text);
        len -= n;
        pokeChars(text, addr, n);
        addr = u16(addr + 0x80);
        lines++;
    }
    if (len) pokeChars(text, addr, len);
    W16(0xFF080A, u16(lines * 0x12C));
    return lines;
}

}  // namespace

void highlightVerb(u16 style) {
    const u16 verb = R16(0xFF06C2);
    if (!verb) return;
    u16 idx = 0;
    while (R16(0x2FE3C + idx * 2) != verb) idx++;
    u16 off = 0x84;
    if (idx > 4) { off = 0x184; idx = u16(idx - 5); }
    off = u16(off + idx * 10);
    u32 src = 0x30016 + s16(u16(off + style * 0x300));
    u16 addr = u16(0xAA80 + off);
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 4; c++, addr = u16(addr + 2), src += 2) ll::vramPoke(addr, u16(R16(src) + R16(0xFF0802)));
        addr = u16(addr + 0x78);
        src += 0x78;
    }
}

void setArrowUp(u16 state) {
    while (RS8(0xFF0AB1) >= 0) ll::idleFrame();
    W8(0xFF0AB1, 8);
    W16(0xFF0804, state);
}

void setArrowDown(u16 state) {
    while (RS8(0xFF0AB2) >= 0) ll::idleFrame();
    W8(0xFF0AB2, 8);
    W16(0xFF0806, state);
}

void updatePageArrows() {
    setArrowUp(R16(0xFF06EC) == 0 ? 2 : 0);
    u16 carried = 0;
    for (u32 o = 0xFF1200, n = 0; n <= R16(0xFF06F4); n++, o += 0x1A)
        if (R16(o + 6) == 1) carried++;
    u16 state = 2;
    if (carried && u16((carried - 1) >> 2) != R16(0xFF06EC)) state = 0;
    setArrowDown(state);
}

void buildInventoryCells() {
    const u32 cells = 0xFF0328;
    auto cellAddr = [&](int cell) {
        u32 a = cells + (cell & 3) * 10;
        if (cell >= 4) a += 0x78;
        return a;
    };
    int next = 0;   // first cell still to be filled
    u16 count = 0;
    for (u32 p = 0xFF0A1E; RS16(p) >= 0; p += 2) count++;
    if (count) {
        const u16 lastPage = u16((count - 1) >> 2);
        if (s16(lastPage) < RS16(0xFF06EC)) W16(0xFF06EC, lastPage);
        const u16 first = u16(R16(0xFF06EC) << 2);
        if (s16(first) <= s16(count - 1)) {
            int remaining = count - 1 - first;
            u32 list = 0xFF0A1E + first * 2;
            const u32 icons = R32(R32(RoomHeaderPtr) + 0x18);
            u32 tiles = 0xFF2C00;
            for (int cell = 0; cell < 8; cell++) {
                next = cell + 1;
                const u16 obj = R16(list);
                list += 2;
                const u16 icon = R16(0xFF1200 + s16(u16(u32(obj) * 0x1A)) + 4);
                if (icon) {
                    u32 src = icons + u32(u16(icon - 1)) * 0x182;
                    const u16 pal = R16(src);
                    src += 2;
                    for (int i = 0; i < 0x60; i++, src += 4, tiles += 4) W32(tiles, R32(src));
                    u16 tile = u16((cell * 12 + 0x8580) | pal);
                    u32 a = cellAddr(cell);
                    for (int r = 0; r < 3; r++) {
                        for (int c = 0; c < 4; c++, a += 2, tile = u16(tile + 3)) W16(a, tile);
                        W16(a, 0x8000);
                        a += 0x20;
                        tile = u16(tile - 0x0B);
                    }
                }
                if (--remaining < 0) break;
            }
        }
    }
    for (int cell = next; cell < 8; cell++) {
        u32 a = cellAddr(cell);
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 4; c++, a += 2) W16(a, 0x8000);
            W16(a, 0x8000);
            a += 0x20;
        }
    }
}

void uploadInventory() {
    ll::dmaVram(0xFF2C00, 0xB000, 0x600);
    u32 src = 0xFF0328;
    u16 addr = 0xAACC;
    for (int r = 0; r < 6; r++, addr = u16(addr + 0x80)) src = ll::dmaVram(src, addr, 0x14);
    ll::vramFill(0xAD80, 0x40, 0);
}

void inventoryFrameWork() {
    if (!BTST(EngineFlags, 5)) return;
    BCLR(EngineFlags, 5);
    uploadInventory();
}

void redrawInventory() {
    while (BTST(0xFF0AB6, 0)) ll::idleFrame();
    buildInventoryCells();
    BSET(EngineFlags, 5);
    while (BTST(EngineFlags, 5)) ll::idleFrame();
}

u32 objectName(u16 number) {
    const u32 hdr = R32(RoomHeaderPtr);
    const u32 text = R32(hdr + 0x20);
    if (number == 0) return text;
    if (number == 1) return 0x2FE50;
    if (number == 2) return 0x2FE57;
    return text + R32(R32(hdr + 0x28) + s16(u16((number - 3) << 3)) + 4);
}

void showTopText(u32 text) {
    const u16 blank = R16(0xFF08B8);
    for (int i = 0; i < 0x400; i++) ll::vramPoke(u16(0xD000 + i * 2), blank);
    int len = ll::strLen(text);
    W16(0xFF080A, u16(len));
    u16 addr = 0xD042, lines = 2;
    while (len > 0x1E) {
        const int n = lineBreak(text);
        len -= n;
        pokeChars(text, u16(addr + 0x1E - (n & ~1)), n);
        addr = u16(addr + 0x40);
        lines++;
    }
    if (len) pokeChars(text, u16(addr + 0x1E - (len & ~1)), len);
    W8(0xFF0AD1, u8(lines));
    ll::setVdpReg(0x12, lines);
    W16(0xFF080A, u16(R16(0xFF080A) << 4));
}

void clearTopText() {
    W8(kText, 0);
    showTopText(kText);
}

void talkGesture() {
    if (BTST(0xFF09E9, 7) || BTST(0xFF09E9, 4)) return;
    s16 r = s16(ll::random(7) - 3);
    if (r < 0) r = 0;
    if (R16(0xFF05B8) == 2) W16(0xFF05B8, 0);
    u16 facing = R16(0xFF05B8);
    if (facing == 3) facing = 2;
    W16(0xFF0488, u16(r + facing * 4 + 0x24));
    BSET(0xFF0AB5, 0);
}

void waitMessage() {
    if (autoplay::active) { actor::update(); ll::waitVBlank(); return; }
    W16(0xFF080A, R16(0xFF080A) >> 1);
    while (!BTST(PadState, 6) || !BTST(PadState, 4)) ll::idleFrame();
    for (;;) {
        actor::update();
        actor::objectTicks();
        if (BTST(0xFF0ACA, 1) && BTST(0xFF0ACA, 0)) return;
        if (BTST(0xFF09DE, 4) && BTST(0xFF0ABC, 0)) talkGesture();
        ll::waitVBlank();
        const u16 step = R16(0xA182 + R16(0xFF080C));
        const u16 left = R16(0xFF080A);
        W16(0xFF080A, u16(left - step));
        if (left < step) return;
        if (!BTST(PadState, 6)) {
            while (!BTST(PadState, 6)) ll::idleFrame();
            return;
        }
        if (!BTST(PadState, 4)) {
            while (!BTST(PadState, 4)) ll::idleFrame();
            return;
        }
    }
}

void showMessage(u32 text) {
    const u8 cursor = R8(EngineFlags) & 8;
    BCLR(EngineFlags, 3);
    showTopText(text);
    waitMessage();
    clearTopText();
    M.vdp.regWord(0x9202);
    W8(0xFF0AD1, 2);
    W8(EngineFlags, R8(EngineFlags) | cursor);
    BSET(0xFF09DE, 1);
}

void refreshStatusLine() {
    if (!BTST(EngineFlags, 3)) {
        if (BTST(0xFF09EB, 5)) return;
        if (BTST(0xFF09DE, 1)) BCLR(0xFF09DE, 1);
        else if (!BTST(0xFF09DD, 3) && R32(0xFF06D2) == R32(0xFF06D6)) return;
        showTopText(R32(0xFF06D2));
        return;
    }
    if (BTST(0xFF09DE, 1)) {
        BCLR(0xFF09DE, 1);
    } else if (BTST(0xFF09DD, 3) && R16(0xFF06AE) == R16(0xFF06B4) && R16(0xFF06C0) == R16(0xFF06B8) &&
               R16(0xFF06B6) == R16(0xFF06BC) && !((R8(0xFF09DE) ^ R8(0xFF09DF)) & 1)) {
        return;
    }
    if (R16(0xFF06AE) == 0) {
        W8(kText, 0);
    } else {
        u32 verbName = 0x2FE5E;
        for (u16 n = R16(0xFF06C0); n > 0; n--)
            while (R8(verbName++)) {}
        u32 dst = strCopy(verbName, kText) - 1;
        dst = strCopy(objectName(R16(0xFF06AE)), dst) - 1;
        if (BTST(0xFF09DE, 0)) {
            dst = strCopy(BTST(0xFF09E8, 0) ? 0x2FFEF : 0x2FFE8, dst) - 1;
            if (R16(0xFF06B6)) strCopy(objectName(R16(0xFF06B6)), dst);
        }
    }
    showTopText(kText);
}

u16 savePanel() {
    BCLR(EngineFlags, 3);
    const u16 addr = panelTextAddr();
    savePanelRows(addr);
    const u16 blank = u16((R16(TextAttrBase) & 0x7FF) + 0x20);
    u16 a = addr;
    for (int r = 0; r < 6; r++, a = u16(a + 0x40))
        for (int c = 0; c < 0x20; c++, a = u16(a + 2)) ll::vramPoke(a, blank);
    return addr;
}

void openTextBox() {
    BSET(0xFF2A00, 1);
    while (RS8(0xFF0ACB) >= 0) ll::idleFrame();
    const u16 addr = savePanel();
    slidePanel();
    W16(0xFF0876, addr);
}

void closeTextBox() {
    const u16 addr = R16(0xFF0876);
    slidePanel();
    restorePanelRows(addr);
    ll::loadFont(0x0F, 1, R16(TextAttrBase) & 0x9FFF);
    BCLR(0xFF2A00, 1);
}

void collectChoices() {
    W8(0xFF0AC9, 4);
    W16(0xFF080E, 0);
    const u32 pc = vm::pc, end = vm::end, loop = R32(0xFF0878);
    W32(0xFF0878, vm::ScanLoop);
    vm::runScan();
    W32(0xFF0878, loop);
    vm::pc = pc;
    vm::end = end;
}

void dialogue() {
    collectChoices();
    if (R16(0xFF080E) == 0) return;
    clearTopText();
    W16(0xFF06EE, 0xFFFF);
    BSET(0xFF09DE, 2);
    while (RS8(0xFF0ACB) >= 0) ll::idleFrame();
    BCLR(EngineFlags, 3);
    const u16 panel = panelTextAddr();
    savePanelRows(panel);
    clearPanelText(panel);
    slidePanel();

    struct Level { u16 used; u32 pc, end; };
    std::vector<Level> stack;
    W32(0xFF0812, vm::pc);
    W32(0xFF0816, vm::end);
    W16(0xFF0810, 0);
    W16(0xFF0874, 0);
    auto used = [](int i) { return BTST(0xFF0874, i & 7); };

    for (;;) {
        vm::pc = R32(0xFF0812);
        vm::end = R32(0xFF0816);
        collectChoices();
        bool leaveLevel = R16(0xFF080E) == 0;
        if (!leaveLevel) {
            clearPanelText(panel);
            W16(TextAttrBase, R16(TextAttrBase) & 0x9FFF);
            ll::loadFont(6, 0, R16(TextAttrBase));
            while (!BTST(PadState, 6) || !BTST(PadState, 4)) ll::idleFrame();
            u16 row = 0;
            for (int i = 0, left = RS16(0xFF080E);; ) {
                if (!used(i)) {
                    const u16 n = drawPanelText(row, BTST(EngineFlags, 4) ? 0x20 : 0, R32(0xFF0838 + i * 4));
                    W16(0xFF081A + i * 2, n);
                    row = u16(row + n);
                }
                i++;
                if (--left <= 0 || s16(row) >= 5) break;
            }
            u16 cursorRow = 0;
            W16(0xFF06F2, u16((R16(0xFF081A) - 1) << 8));
            W16(0xFF06F0, 0);
            W16(0xFF06EE, 0);
            u16 sel = 0;
            for (int i = 4; i >= 0; i--)
                if (!used(i)) sel = u16(i);
            for (;;) {
                BSET(EngineFlags, 0);
                actor::update();
                if (!BTST(PadState, 0) && BTST(PadPrev, 0) && sel != 0) {
                    for (int i = sel - 1; i >= 0; i--)
                        if (!used(i)) { sel = u16(i); cursorRow = u16(cursorRow - R16(0xFF081A + i * 2)); break; }
                } else if (!BTST(PadState, 1) && BTST(PadPrev, 1)) {
                    for (int i = sel + 1; i < RS16(0xFF080E) && i < 5; i++)
                        if (!used(i)) { cursorRow = u16(cursorRow + R16(0xFF081A + sel * 2)); sel = u16(i); break; }
                }
                W16(0xFF06F0, cursorRow);
                W16(0xFF06F2, u16((R16(0xFF081A + sel * 2) - 1) << 8));
                M.yieldFrame();   // the bit set above is the frame wait
                if (autoplay::active) {   // the solver picks by its plan for this conversation
                    std::vector<u16> open;
                    for (int i = 0; i < RS16(0xFF080E) && i < 5; i++)
                        if (!used(i)) open.push_back(u16(i));
                    if (!open.empty()) sel = open[size_t(autoplay::chooseLine(int(open.size())))];
                    break;
                }
                if (!BTST(PadState, 4) || !BTST(PadState, 6)) break;
            }
            W16(0xFF06EE, 0xFFFF);
            BSET(0xFF0874, sel & 7);
            clearPanelText(panel);
            W16(TextAttrBase, R16(TextAttrBase) & 0x9FFF);
            ll::loadFont(6, 1, R16(TextAttrBase));
            showTopText(R32(0xFF0838 + sel * 4));
            talkGesture();
            BSET(0xFF09DE, 4);
            waitMessage();
            BCLR(0xFF09DE, 4);
            W16(0xFF0488, u16(R16(0xFF05B8) + 4));
            BSET(0xFF0AB5, 0);

            W16(0xFF06B2, R16(0xFF0824 + sel * 2));
            vm::pc = R32(0xFF0860 + sel * 4);
            vm::end = vm::pc + RS16(0xFF082E + sel * 2);
            W32(0xFF0878, vm::ExecLoop);
            const u32 reply = R32(0xFF084C + sel * 4);
            clearTopText();
            const u8 colour = R8(0xFF06B1);
            W16(TextAttrBase, u16(R16(TextAttrBase) | ((colour & 0x30) << 9)));
            ll::loadFont(colour & 0x0F, 1, R16(TextAttrBase));
            if (RS16(0xFF06AE) - 3 >= 0) {
                const s8 slot = RS8(vm::objectAddr(R16(0xFF06AE)) + 0x19);
                if (slot >= 0 && RS16(0xFF06B2) >= 0) {
                    W16(0xFF048C + slot * 2, R16(0xFF06B2));
                    W16(0xFF061C + slot * 2, 0xFFFF);
                    BSET(0xFF0AB5, (slot + 2) & 7);
                }
            }
            showMessage(reply);
            clearTopText();
            W8(0xFF0AC9, 0);
            const u32 blockStart = vm::pc;
            vm::runExec();
            vm::pc = blockStart;
            if (BTST(0xFF0AC9, 0)) {
                leaveLevel = true;
            } else if (BTST(0xFF0ACA, 2)) {
                BCLR(0xFF0ACA, 2);
                break;
            } else {
                W16(0xFF0810, u16(R16(0xFF0810) + 1));
                stack.push_back({R16(0xFF0874), R32(0xFF0812), R32(0xFF0816)});
                W32(0xFF0812, vm::pc);
                W32(0xFF0816, vm::end);
                W16(0xFF0874, 0);
                continue;
            }
        }
        if (leaveLevel) {
            W16(0xFF0810, u16(R16(0xFF0810) - 1));
            if (RS16(0xFF0810) < 0 || stack.empty()) break;
            W32(0xFF0816, stack.back().end);
            W32(0xFF0812, stack.back().pc);
            W16(0xFF0874, stack.back().used);
            stack.pop_back();
        }
    }
    W16(0xFF06EE, 0xFFFF);
    clearPanelText(panel);
    slidePanel();
    restorePanelRows(panel);
    ll::loadFont(0x0F, 1, R16(TextAttrBase) & 0x9FFF);
    BCLR(0xFF09DE, 2);
    ll::waitVBlank();
    redrawInventory();
    updatePageArrows();
}

}  // namespace ui
