#include "room.h"
#include "actors.h"
#include "addr.h"
#include "interrupts.h"
#include "lowlevel.h"
#include "script.h"
#include "ui.h"

namespace room {

namespace {

constexpr u32 kMapB = 0xFF7000, kMapA = 0xFF8C00;        // live room maps
constexpr u32 kBackB = 0xFFA800, kBackA = 0xFFC400;      // pristine copies
constexpr u32 kCollision = 0xFFE000;

u16 roomW() { return R16(0xFF06C6); }
u16 roomH() { return R16(0xFF06C8); }

bool pixelBlocked(u16 x, u16 y, u16 attrMask) {
    const u16 cell = u16((x >> 3) * 2 + u32(y >> 3) * u16(roomW() * 2));
    u16 v = R16(kCollision + s16(cell)) & attrMask;
    if (!v) return false;
    v = R16(0x2AF8E + s16(u16(v * 2)));
    const u32 tile = 0x2B0BA + s16(u16((v & 0x7FF) << 5));
    u16 px = x & 7, py = y & 7;
    if (!(v & 0x0800)) px ^= 7;
    if (v & 0x1000) py ^= 7;
    return (R32(tile + py * 4) & (0xFu << (px * 4))) != 0;
}

// Runs one of the room's two scripts (after-load at +0x0C, after-fade at +0x10).
void runRoomScript(int field) {
    const u32 savedPc = vm::pc, savedEnd = vm::end;
    const u16 savedVerb = vm::verb;
    const u32 savedLoop = R32(0xFF0878);
    const u8 savedFlags = R8(EngineFlags);
    W32(0xFF0878, vm::ExecLoop);
    const u32 hdr = R32(RoomHeaderPtr);
    const u32 entry = R32(hdr + 8) + s16(u16(u32(u16(R16(0xFF06AC) - 2)) * 0x14));
    u32 p = R32(hdr + 0x2C) + R32(entry + field);
    const s16 len = RS16(p);
    p += 4;
    vm::pc = p;
    vm::end = p + len;
    BSET(EngineFlags, 7);
    vm::runExec();
    W8(EngineFlags, R8(EngineFlags) | (savedFlags & 0x80));
    W32(0xFF0878, savedLoop);
    vm::pc = savedPc;
    vm::end = savedEnd;
    vm::verb = savedVerb;
}

// One column of 21 rows for both planes. `col` is the room column, the
// source starts one row above the camera row.
void columnUpdate(u16 col) {
    u16 row = R16(CameraY) >> 3;
    const u16 stride = u16(roomW() * 2);
    const u16 off = u16(col * 2 + u32(row) * stride - stride);
    u32 b = kMapB + s16(off), a = kMapA + s16(off);
    row = (row + 1) & 0x1F;
    const u16 pc = col & 0x3F;
    u16 n = u16(0x20 - row);
    if (s16(n) > 0x15) n = 0x15;
    M.vdp.regWord(0x8F80);
    auto put = [&](u16 addr, u16 count) {
        for (u16 i = 0; i < count; i++, addr = u16(addr + 0x80)) {
            ll::vramPoke(u16(0xE000 + addr), R16(b));
            if (!BTST(0xFF09DE, 6)) ll::vramPoke(u16(0xC000 + addr), R16(a));
            b += stride;
            a += stride;
        }
    };
    put(u16(pc * 2 + (row << 7)), n);
    if (s16(n - 0x15) < 0) put(u16(pc * 2), u16(0x15 - n));
    M.vdp.regWord(0x8F02);
}

// One row of 34 cells for both planes, starting one column left of the camera.
void rowUpdate(u16 srcRow, u16 planeRow) {
    const u16 col = R16(CameraX) >> 3;
    const u16 stride = u16(roomW() * 2);
    const u16 off = u16(col * 2 + u32(srcRow) * stride);
    u32 b = kMapB + s16(off) - 2, a = kMapA + s16(off) - 2;
    const u16 pc = (col - 1) & 0x3F;
    const u16 base = u16((planeRow & 0x1F) << 7);
    u16 n = u16(0x40 - pc);
    if (s16(n) > 0x22) n = 0x22;
    auto put = [&](u16 addr, u16 count) {
        for (u16 i = 0; i < count; i++, addr = u16(addr + 2)) {
            ll::vramPoke(u16(0xE000 + addr), R16(b));
            if (!BTST(0xFF09DE, 6)) ll::vramPoke(u16(0xC000 + addr), R16(a));
            b += 2;
            a += 2;
        }
    };
    put(u16(pc * 2 + base), n);
    if (s16(n - 0x22) < 0) put(base, u16(0x22 - n));
}

void scrollRight() {   // 0xFC3A
    const u16 col = R16(CameraX) >> 3;
    if (col != u16(roomW() - 0x20)) columnUpdate(u16(col + 0x20));
    else M.vdp.regWord(0x8F02);
}
void scrollLeft() {    // 0xFD5E
    const u16 col = R16(CameraX) >> 3;
    if (col != 0) columnUpdate(u16(col - 1));
    else M.vdp.regWord(0x8F02);
}
void scrollUp() {      // 0xFE74
    const u16 row = R16(CameraY) >> 3;
    if (row != 0) rowUpdate(u16(row - 1), u16(row + 1));
}
void scrollDown() {    // 0xFF68
    const u16 row = R16(CameraY) >> 3;
    if (row != u16(roomH() - 0x13)) rowUpdate(u16(row + 0x13), u16(row + 0x15));
}

u32 hotspot(u16 index) {
    const u32 hdr = R32(RoomHeaderPtr);
    return R32(R32(hdr + 0x24) + s16(u16((index - 1) << 2))) + R32(hdr + 0x1C);
}

// 0x7C82: patch drawn straight to video memory (used on fixed screens).
void drawPatchDirect(u16 state) {
    if (state == 0) return;
    const bool erase = state & 0x8000;
    const u32 r = hotspot(state & 0x7FFF);
    const bool planeA = R16(r + 0x0C) != 0;
    const u16 w = R16(r + 4);
    if (!erase) {
        u16 addr = u16(R16(r) * 2 + (R16(r + 2) << 7) + (planeA ? 0xC100 : 0xE100));
        u32 src = r + 0x0E;
        for (int y = R16(r + 6); y > 0; y--, addr = u16(addr + 0x80)) src = ll::dmaVram(src, addr, w);
    } else {
        const u16 off = u16(R16(r) * 2 + (R16(r + 2) << 6));
        u16 addr = u16(off + (R16(r + 2) << 6) + (planeA ? 0xC100 : 0xE100));
        u32 src = (planeA ? kBackA : kBackB) + s16(off);
        for (int y = R16(r + 6); y > 0; y--, addr = u16(addr + 0x80)) {
            src = ll::dmaVram(src, addr, w);
            src += s16(u16((0x20 - w) * 2));
        }
    }
}

// 0x7D54: writes an object's picture (or the pristine background, to erase
// it) into the room map and into the visible part of the plane.
void drawPatch(u16 state) {
    if (BTST(0xFF09EB, 0)) return drawPatchDirect(state);
    if (state == 0) return;
    const bool erase = state & 0x8000;
    const u32 r = hotspot(state & 0x7FFF);
    const bool planeA = R16(r + 0x0C) != 0;
    const u16 stride = u16(roomW() * 2);
    const u16 x0 = R16(r), y0 = R16(r + 2), w = R16(r + 4), h = R16(r + 6);
    const u16 off = u16(u32(y0) * stride + x0 * 2);
    const u32 dst = (planeA ? kMapA : kMapB) + s16(off);
    u32 src = erase ? (planeA ? kBackA : kBackB) + s16(off) : r + 0x0E;
    const u16 gap = u16(stride - w * 2);
    u32 d = dst;
    for (u16 y = 0; y < h; y++) {
        for (u16 x = 0; x < w; x++, d += 2, src += 2) W16(d, R16(src));
        d += s16(gap);
        if (erase) src += s16(gap);
    }
    const s16 camCol = s16(R16(CameraX) >> 3), camRow = s16(R16(CameraY) >> 3);
    u32 p = dst;
    for (s16 ry = s16(y0 - camRow); ry != s16(y0 + h - camRow); ry++) {
        for (s16 rx = s16(x0 - camCol); rx != s16(x0 + w - camCol); rx++, p += 2) {
            if (rx < -1 || ry < -1 || rx >= 0x21 || ry >= 0x15) continue;
            u16 addr = u16((u16(camRow + ry) << 7) & 0xFFF);
            addr = u16(addr + ((u16(camCol + rx) << 1) & 0x7F));
            if (planeA) {
                addr = u16(addr + 0xC100);
                if (s16(addr) >= s16(0xD000)) addr = u16(addr - 0x1000);
            } else {
                addr = u16(addr + 0xE100);
                if (s16(addr) >= s16(0xF000)) addr = u16(addr - 0x1000);
            }
            ll::vramPoke(addr, R16(p));
        }
        p += s16(gap);
    }
}

}  // namespace

bool blockedProbe() {
    const s16 x = RS16(0xFF064A), y = RS16(0xFF064E);
    if (x < 0 || y < 0) return true;
    return pixelBlocked(u16(x), u16(y), 0x07FF);
}

bool blockedTrial(u16 maxX, u16 maxY) {
    const s16 x = RS16(0xFF0642), y = RS16(0xFF0646);
    if (x < 0 || y < 0 || s16(maxX) < x || s16(maxY) < y) return true;
    return pixelBlocked(u16(x), u16(y), 0x7FFF);
}

void scrollUpdates() {
    const u8 f = R8(0xFF0AB3);
    if (f & 1) scrollRight();
    if (f & 2) scrollLeft();
    if (f & 4) scrollUp();
    if (f & 8) scrollDown();
}

void restorePanel() {
    const u16 col = R16(CameraX) >> 3, row = R16(CameraY) >> 3;
    const u16 stride = u16(roomW() * 2);
    const u16 off = u16(col * 2 + u32(row * 2) * roomW());
    for (int plane = 0; plane < 2; plane++) {
        u32 src = (plane ? kMapA : kMapB) + s16(off);
        u16 rowAddr = u16((row + 2) << 7);
        for (int y = 0; y <= 0x12; y++) {
            rowAddr &= 0xFFF;
            u16 x = u16(col * 2);
            for (int c = 0; c < 0x20; c++) {
                x &= 0x7F;
                ll::vramPoke(u16(rowAddr + x + (plane ? 0xC000 : 0xE000)), R16(src));
                src += 2;
                x = u16(x + 2);
            }
            src += u32(stride) - 0x40;
            rowAddr = u16(rowAddr + 0x80);
        }
    }
    scrollRight();
    scrollLeft();
    scrollUp();
    scrollDown();
}

void resetActors() {
    W8(0xFF0ABF, R8(0xFF0ABF) & 3);
    W8(0xFF0AC3, 0);
    W8(0xFF0AC0, 0);
    W16(0xFF0688, 0x64);
    BCLR(0xFF09DE, 5);
    for (int i = 0; i < 6; i++) {
        W16(0xFF050C + i * 2, 0xFFFF);
        W16(0xFF0618 + i * 2, 0xFFFF);
    }
    W8(0xFF0ACC, 0);
    W8(0xFF0AB7, R8(0xFF0AB7) & 0xFC);
    W16(0xFF0686, 0x12C);
    W16(0xFF0488, u16(4 + R16(0xFF05B8)));
    if (!BTST(0xFF09DE, 7)) W16(0xFF048A, u16(4 + R16(0xFF05B8)));
    W8(0xFF0ABE, 0);
    u8 active = 3, request = 3, scalable = 3;
    int slot = 2;
    for (u32 o = 0xFF1200, n = 0; n <= R16(0xFF06F4) && slot < 6; n++, o += 0x1A) {
        if (!BTST(o + 0x18, 1)) continue;
        W8(o + 0x19, 0xFF);
        if (R16(o + 6) != R16(0xFF06AC)) continue;
        const u16 id = R16(o + 2);
        u32 entry = 0x32670, base = 0;
        for (int i = 0; i < 0x24; i++, entry += 4)
            if (R16(R32(entry)) == id) { base = R32(entry); break; }
        if (!base) continue;
        const int k = slot - 2;
        active |= u8(1 << slot);
        if (BTST(o + 0x18, 3)) scalable |= u8(1 << slot);
        const u32 speed = R32(entry + 4 - 0x94);
        W32(0xFF051C + k * 4, speed);
        W32(0xFF0530 + k * 4, speed >> 1);
        request |= u8(1 << slot);
        W8(o + 0x19, u8(k));
        W16(0xFF04E4 + k * 4, R16(o + 0x12));
        W16(0xFF04FC + k * 4, R16(o + 0x14));
        W16(0xFF05E0 + k * 2, R16(o + 0x12));
        W16(0xFF05EC + k * 2, R16(o + 0x14));
        W16(0xFF048C + k * 2, R16(o));
        W32(0xFF0478 + k * 4, base);
        slot++;
    }
    W8(0xFF0AB4, active);
    W8(0xFF0AB5, request);
    W8(0xFF0AB8, scalable);
    W8(0xFF0AB6, 0);
    W8(0xFF0AB9, 0);
    if (BTST(0xFF09DE, 7)) BSET(0xFF0ABF, 1);
}

void objectPass() {
    u32 o = 0xFF1200;
    for (u32 n = 0; n <= R16(0xFF06F4); n++, o += 0x1A) {
        if (RS16(o + 0x0A) >= 0) {
            W16(o + 0x0A, u16(R16(o + 0x0A) - 1));
            if (RS16(o + 0x0A) < 0) BSET(o + 0x18, 2);
            else BCLR(o + 0x18, 2);
        }
        if (!BTST(o + 0x18, 0) || BTST(o + 0x18, 1)) continue;
        const u16 state = R16(o) & 0xBFFF;
        drawPatch(state);
        if (state & 0x8000) {
            if (BTST(o, 6)) W16(o, 0);
            W16(o, R16(o) & 0x7FFF);
        }
        BCLR(o + 0x18, 0);
    }
}

void load() {
    ll::vramClear(0, 0x8000);
    W16(CameraX, 0);
    W16(CameraY, 0);
    W32(ScrollAY, 0);
    W32(ScrollAX, 0);
    for (int i = 0; i < 32; i++) W16(TargetPalette + 2 * i, R16(0x32480 + 2 * i));
    const u16 number = R16(0xFF06AC);
    for (u32 o = 0xFF1200, n = 0; n <= R16(0xFF06F4); n++, o += 0x1A) {
        if (R16(o + 6) == number) BSET(o + 0x18, 0);
        else BCLR(o + 0x18, 0);
    }
    const s16 idx = s16(number - 2);
    const u32 hdr = R32(RoomHeaderPtr);
    const u32 pal = R32(hdr + 0x14) + (u32(s32(idx)) << 7) + 0x40;
    for (int i = 0; i < 32; i++) W16(0xFF07B8 + 2 * i, R16(pal + 2 * i));
    BCLR(0xFF09DE, 6);
    W8(0xFF0ACD, 0);
    const u32 entry = R32(hdr + 8) + u32(u16(idx)) * 0x14;

    // Room tiles, then the font and the panel graphics packed in after them.
    ll::lzssDecode(R32(entry + 4) + R32(hdr + 0x0C) + 4, 0xFF3000, 0xFF4000);
    u32 size = R32(0xFF3000);
    if (s32(size) < 0x400) { size = 0x400; W32(0xFF3000, size); }
    ll::vramWrite(0, u16(size) >> 1, 0xFF4000);
    const u16 fontAttr = u16(((u16(size) >> 5) - 0x20) | 0x8000);
    W16(TextAttrBase, fontAttr);
    ll::loadFont(0x0F, 1, fontAttr);
    W16(0xFFFFFE, u16((((fontAttr & 0x7FF) + 0x20) << 5) + 0x5F0 * 2));
    u16 addr = u16(u16(size) + 0xBE0);
    const u16 panelBase = u16((addr >> 5) | 0x8000);
    W16(0xFF0802, panelBase);
    ll::lzssDecode(0x30916, 0xFF3000, 0xFF4000);
    const u16 words = u16(R32(0xFF3000) >> 1);
    ll::vramWrite(addr, words, 0xFF4000);
    addr = u16(addr + words * 2);
    ll::vramWrite(addr, 0x40, 0x31AFA);
    W16(0xFF06E4, u16((addr >> 5) | 0x8000));
    addr = u16(addr + 0x80);
    W16(0xFF06E8, addr >> 5);
    ll::vramWrite(addr, 0xC0, 0x31B7A);
    addr = u16(addr + 0xC0);
    W16(0xFF06E6, addr >> 5);
    ll::vramWrite(addr, 0x80, 0x31CFA);
    addr = u16(addr + 0x100);
    W16(0xFF08B8, u16((addr >> 5) | 0x8000));
    ll::vramFill(addr, 0x10, 0x1111);
    W16(0xFF068A, u16(((addr >> 5) + 1) | 0x8000));
    ll::vramFill(0xD000, 0x400, R16(0xFF08B8));

    // Verb panel map: six rows of 61 cells, padded with the first cell.
    u16 a = 0xAA80;
    u32 src = 0x30016;
    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 0x3D; c++, a = u16(a + 2), src += 2) ll::vramPoke(a, u16(R16(src) + panelBase));
        src += 6;
        ll::vramFill(a, 3, u16(R16(0x30016) + panelBase));
        a = u16(a + 6);
    }
    ui::buildInventoryCells();
    ui::uploadInventory();
    if (BTST(0xFF2A00, 1) || BTST(0xFF09DE, 2)) {
        BCHG(EngineFlags, 4);
        ui::savePanel();
        BCHG(EngineFlags, 4);
    }
    W32(0xFF06D2, R32(entry) + R32(hdr + 0x20));

    // Maps: plane B, plane A, collision, then the depth-scale and point tables.
    const u32 maps = R32(entry + 8) + R32(hdr + 0x10);
    W16(0xFF06C6, R16(maps));
    W16(0xFF06C8, R16(maps + 2));
    const u32 mapB = maps + 8;
    const u32 mapA = mapB + R32(mapB - 4) + 4;
    ll::lzssDecode(mapA, 0xFF3000, kMapA);
    for (int i = 0; i < 0x1800; i++) W16(kBackA + 2 * i, R16(kMapA + 2 * i));   // original copies a fixed 0x3000 bytes
    ll::lzssDecode(mapB, 0xFF3000, kMapB);
    for (u32 i = 0; i < (u16(R32(0xFF3000)) >> 1); i++) W16(kBackB + 2 * i, R16(kMapB + 2 * i));
    const u32 coll = mapA + R32(mapA - 4) + 4;
    ll::lzssDecode(coll, 0xFF3000, kCollision);
    const u32 scale = coll + R32(coll - 4);
    W32(0xFF0692, scale);
    const u32 points = scale + s16(u16(roomH() * 2));
    W32(0xFF0696, points);
    W32(0xFF069A, points + 0x20);
    u16 nodes = 0;
    for (u32 p = points + 0x20; nodes < 8 && R32(p) != 0xFFFFFFFF; p += 4) nodes++;
    W16(0xFF06A0, nodes);

    const u32 start = points + RS16(0xFF069E);
    if (!BTST(0xFF09DE, 3)) {
        const u16 x = u16(R16(start) << 3), y = u16(R16(start + 2) << 3);
        W16(0xFF04DC, x);
        W16(0xFF04E0, x);
        W16(0xFF04F4, y);
        W16(0xFF04F8, u16(y - 1));
    }
    s16 cx = s16((R16(0xFF04DC) >> 3) - 0x10), cy = s16((R16(0xFF04F4) >> 3) - 0x0A);
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    const s16 overX = s16(cx + 0x20 - s16(roomW())), overY = s16(cy + 0x13 - s16(roomH()));
    if (overX > 0) cx = s16(cx - overX);
    if (overY > 0) cy = s16(cy - overY);
    W16(CameraX, u16(cx << 3));
    W16(CameraY, u16(cy << 3));
    restorePanel();
    resetActors();
}

void enter() {
    W8(PadState, 0xFF);
    BCLR(EngineFlags, 3);
    load();
    BSET(EngineFlags, 2);
    BSET(EngineFlags, 6);
    if (!BTST(0xFF2A00, 1)) ui::updatePageArrows();
    do {
        actor::update();
        ll::waitVBlank();
    } while (R8(0xFF0AB6) != 0);
    runRoomScript(0x0C);
}

void show() {
    BSET(0xFF09DE, 1);
    ui::refreshStatusLine();
    ll::waitVBlank();
    BCLR(EngineFlags, 6);
    ll::fadeIn(TargetPalette);
    BSET(EngineFlags, 6);
    W16(0xFF06B4, R16(0xFF06AE));
    W8(0xFF09DD, R8(EngineFlags));
    W8(0xFF09DF, R8(0xFF09DE));
    W32(0xFF06D6, R32(0xFF06D2));
    W16(0xFF06B8, R16(0xFF06C0));
    W16(0xFF06BC, R16(0xFF06B6));
    runRoomScript(0x10);
}

void enterAndShow() {
    enter();
    show();
}

}  // namespace room

namespace irq {
void roomScrollUpdates() { room::scrollUpdates(); }
}  // namespace irq
