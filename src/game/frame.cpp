// Work done by the gameplay frame interrupt (0xA684-0xA6BC): palette cycles,
// actor tile uploads, the sprite table, interface icons, object redraws and
// the big per-frame control routine at 0x5E4A.
#include "actors.h"
#include "addr.h"
#include "lowlevel.h"
#include "room.h"
#include "script.h"
#include "ui.h"

namespace irq {

namespace {

// 0xA7CE: up to five palette ranges rotate by one entry when their timer runs out.
void paletteCycles() {
    for (int slot = 4; slot >= 0; slot--) {
        if (!BTST(0xFF0ACD, slot)) continue;
        const u32 t = 0xFF089A + slot * 2;
        W16(t, u16(R16(t) - 1));
        if (RS16(t) >= 0) continue;
        W16(t, R16(0xFF0890 + slot * 2));
        const u16 first = R16(0xFF087C + slot * 2);
        const u16 n = u16(R16(0xFF0886 + slot * 2) - first);
        const u32 a = WorkPalette + u32(first) * 2;
        const u16 head = R16(a);
        for (u32 i = 0; i < n; i++) W16(a + 2 * i, R16(a + 2 * i + 2));
        W16(a + 2 * u32(n), head);
        ll::dmaCram(a, u16(first * 2), u16(n + 1));   // the destination is a byte offset into colour RAM
    }
}

// 0xAF24: at most one actor's tiles go to video memory per frame.
void uploadActorTiles() {
    for (int i = 0; i < 6; i++) {
        if (!BTST(0xFF0AB4, i) || !BTST(0xFF0AB6, i)) continue;
        ll::dmaVram(R32(0xFF0540 + i * 4), R16(0xFC860 + i * 2), R16(0xFF0588 + i * 2));
        BCLR(0xFF0AB6, i);
        BCLR(0xFF0AC1, i);
        BSET(0xFF0ABD, i);
        break;
    }
}

struct Layout { u32 x, xFlip, y, tile, size; int count; };

// 0xAD16: one actor as a grid of hardware sprites.
void actorSprites(int i, u32& out, u16& link) {
    const u32 hdrSlot = 0xFF0558 + i * 4, v = 0xFF04A0 + i * 2;
    const u16 tileBase = R16(0xFC860 + i * 2) >> 5;
    if (!BTST(0xFF0AC1, i) && !BTST(0xFF0AB9, i) && !BTST(0xFF0AB6, i)) {
        BSET(0xFF0AC1, i);
        W32(hdrSlot, R32(hdrSlot + 0x18));
        W16(v + 0x124, R16(v + 0x100));
        W16(v + 0x130, R16(v + 0x10C));
        W16(v + 0x24, R16(v + 0x0C));
        W16(v + 0x30, R16(v + 0x18));
    }
    if (R8(hdrSlot) & 0x80) return;
    const u32 h = R32(hdrSlot);
    s16 x = s16(R16(0xFF04DC + i * 4) - R16(CameraX));
    s16 y = s16(R16(0xFF04F4 + i * 4) - R16(CameraY));
    if (x < -0x50 || y < -0x50 || x > 0x150 || y > 0xE8) return;
    x = s16(x + 0x80);
    y = s16(y + 0x90);
    if (BTST(0xFF0AB8, i)) { x = s16(x - R16(v + 0x24)); y = s16(y - R16(v + 0x30)); }
    else { x = s16(x - R16(h)); y = s16(y - R16(h + 2)); }
    Layout L;
    switch (R8(h + 4)) {
        case 0x70: L = {0xFC86C, 0xFC87C, 0xFC88C, 0xFC968, 0xFC9C0, 8}; break;
        case 0x40: L = {0xFC92C, 0xFC934, 0xFC93C, 0xFC984, 0xFC9DC, 4}; break;
        case 0x30: L = {0xFC8C0, 0xFC8CC, 0xFC8D8, 0xFC950, 0xFC9A8, 6}; break;
        case 0x20:
            if (R8(h + 5) != 0x50) { y = s16(y - 0x4C); L = {0xFC908, 0xFC914, 0xFC920, 0xFC978, 0xFC9D0, 6}; }
            else L = {0xFC8E4, 0xFC8F0, 0xFC8FC, 0xFC95C, 0xFC9B4, 6};
            break;
        default: L = {0xFC89C, 0xFC8A8, 0xFC8B4, 0xFC944, 0xFC99C, 6}; break;
    }
    const u32 xs = BTST(h + 0x0A, 3) ? L.xFlip : L.x;
    for (int k = 0; k < L.count; k++) {
        x = s16(x + RS16(xs + k * 2));
        y = s16(y + RS16(L.y + k * 2));
        W16(out, u16(y));
        W16(out + 6, u16(x));
        u16 attr = u16((tileBase + R16(L.tile + k * 2)) | R16(h + 0x0A));
        if (BTST(0xFF0ABB, i)) attr |= 0x8000;
        W16(out + 4, attr);
        W16(out + 2, u16(link | R16(L.size + k * 2)));
        out += 8;
        link++;
    }
}

// 0xA984: builds the sprite table in RAM.
void buildSprites() {
    W8(0xFF0AC5, R8(0xFF0AC4));
    W8(0xFF0AC4, 0);
    u16 link = 1;
    u32 out = SpriteBuffer;
    auto emit = [&](u16 y, u16 sizeLink, u16 tile, u16 x) {
        W16(out, y); W16(out + 2, sizeLink); W16(out + 4, tile); W16(out + 6, x);
        out += 8;
    };
    const bool mouse = M.mouseEnabled && M.mouseActive;
    if (mouse) {   // addition: with a mouse the arrow is always on screen, on top
        emit(u16(M.mouseY - 8 + 0x80), u16(0x0500 | link), R16(0xFF06E4), u16(M.mouseX - 8 + 0x80));
        link++;
    }
    if (BTST(EngineFlags, 3)) {   // pointer
        const s16 cy = RS16(CursorY);
        if (cy >= 0xA8) {
            if (RS8(0xFF0ACB) < 0) {
                const bool inv = BTST(EngineFlags, 4);
                u16 sy = 0x128;
                if (cy >= 0xC0) sy = 0x140;
                else if (!inv) sy = 0x130;
                s16 cell = 0;
                for (s16 t = RS16(CursorX); (t = s16(t - 0x28)) >= 0;) cell = s16(cell + 0x28);
                if (!inv) { cell = s16(cell + 8); if (cell > 0xA8) cell = 0xA8; }
                else { if (cell > 0xC8) cell = 0xC8; if (cell == 0) cell = 0x28; }
                if (R16(0xFF06E0) != 2) W16(CursorX, u16(cell + 0x14));
                emit(sy, u16((inv ? 0x0E00 : 0x0D00) | link), R16(inv ? 0xFF06E8 : 0xFF06E6), u16(cell + 0x88));
                link++;
            }
        } else {
            W16(0xFF06E0, 2);
            W16(0xFF06E2, 2);
            if (!mouse) {
                emit(u16(cy + 0x80), 0x0501, R16(0xFF06E4), u16(R16(CursorX) + 0x80));
                link++;
            }
        }
    }
    if (BTST(0xFF09E8, 4)) {   // moving marker following a path table
        u32 p = R32(0xFF08A6);
        const u16 y = u16(R16(p + 0x48) + 0x90), x = u16(R16(p) + 0x90);
        p += 2;
        W32(0xFF08A6, p);
        if (BTST(0xFF09E8, 5) && p == 0x32406) W32(0xFF08A6, p - 2);
        W8(0xFF0ACE, u8(R8(0xFF0ACE) - 1));
        if (RS8(0xFF0ACE) < 0) { W8(0xFF0ACE, 7); BCHG(0xFF09E8, 6); }
        emit(y, u16(link | 0x500), u16((BTST(0xFF09E8, 6) ? 4 : 0) + R16(0xFF08A4)), x);
        link++;
    }
    if (BTST(0xFF09DE, 2) && RS16(0xFF06EE) >= 0) {   // dialogue highlight bar
        u16 x = u16((R16(0xFF06EE) << 3) + 0x80);
        const u16 y = u16((R16(0xFF06F0) << 3) + 0x128);
        for (int k = 0; k < 8; k++, x = u16(x + 0x20)) {
            emit(y, u16(link | 0xC00 | R16(0xFF06F2)), R16(0xFF06E8), x);
            link++;
        }
    }
    if (BTST(0xFF09EA, 0)) {   // mini-game energy bars: four sprites, each four tiles wide (0xAB9C)
        static const u16 tiles[4] = {0x518, 0x51C, 0x520, 0x524}, xs[4] = {0xB0, 0xD0, 0x110, 0x130};
        for (int k = 0; k < 4; k++) emit(0x130, u16((link + k) | 0x0C00), tiles[k], xs[k]);
        link = u16(link + 4);
    }
    // Four masking sprites keep actors from showing over the panel.
    emit(0x128, u16(link | 0x300), 0, 1);
    emit(0x128, u16((link | 0x300) + 1), 0, 0);
    emit(0x148, u16((link | 0x300) + 2), 0, 1);
    emit(0x148, u16((link | 0x300) + 3), 0, 0);
    link = u16(link + 4);

    u8 pending = u8(R8(0xFF0AB4) & ~R8(0xFF0ABF));
    while (pending) {
        int pick = 5;
        s16 best = -0x7FFF;
        for (int i = 5; i >= 0; i--) {
            if (!(pending & (1 << i))) continue;
            if (BTST(0xFF09E9, 2)) { pick = i; break; }
            const s16 y = RS16(0xFF04F4 + i * 4);
            if (y > best) { best = y; pick = i; }
        }
        pending = u8(pending & ~(1 << pick));
        if (!BTST(0xFF0AC5, pick) && !BTST(0xFF0ABD, pick)) continue;
        BSET(0xFF0AC4, pick);
        actorSprites(pick, out, link);
    }
    W8(0xFF0ABA, 0);
    if (out == SpriteBuffer) { W32(SpriteBuffer, 0); W32(SpriteBuffer + 4, 0); }
    else W8(out - 5, 0);
}

// 0xA704: the two inventory page arrows.
void pageArrowIcons() {
    if (BTST(0xFF09DE, 2) || BTST(0xFF2A00, 1)) return;
    auto draw = [](u32 timer, u32 state, u32 art, u16 addr) {
        if (RS8(timer) >= 0) W8(timer, u8(R8(timer) - 1));
        u32 src = art + u32(u16(R16(state) * 0x300));
        for (int r = 0; r < 3; r++, src += 0x80, addr = u16(addr + 0x80)) {
            ll::vramPoke(addr, u16(R16(src) + R16(0xFF0802)));
            ll::vramPoke(u16(addr + 2), u16(R16(src + 2) + R16(0xFF0802)));
        }
    };
    draw(0xFF0AB1, 0xFF0804, 0x3008C, 0xAAF6);
    draw(0xFF0AB2, 0xFF0806, 0x3020C, 0xAC76);
}

void panelArt(u32 src) {   // 7 rows of 10 cells at the right edge of the panel
    u16 addr = 0xAAB6;
    for (int r = 0; r < 7; r++) {
        for (int c = 0; c < 10; c++, addr = u16(addr + 2), src += 2) ll::vramPoke(addr, u16(R16(src) + R16(0xFF0802)));
        addr = u16(addr + 0x6C);
        src += 0x6C;
    }
}

void priorityFromMap(int i, u16 x, u16 y) {
    const u16 cell = u16((x >> 3) * 2 + u32(y >> 3) * u16(R16(0xFF06C6) * 2));
    if (RS16(0xFFE000 + s16(cell)) < 0) BSET(0xFF0ABB, i);
    else BCLR(0xFF0ABB, i);
}

bool pressed(int bit) { return !BTST(PadState, bit); }
bool edge(int bit) { return !BTST(PadState, bit) && BTST(PadPrev, bit); }

// A scripted path for an actor: pairs of words, positions or commands.
bool pathEnded(int i) {
    const u16 w = R16(R32(0xFF05F4 + i * 4) + s16(R16(0xFF060C + i * 2)) - 4);
    return w == 0xFFFE || (w == 0xFFFF && BTST(0xFF0AC3, i));
}

// 0x750A: which of eight directions the pad is asking for.
int padDirection() {
    if (pressed(2)) return pressed(0) ? 1 : pressed(1) ? 3 : 4;
    if (pressed(3)) return pressed(0) ? 2 : pressed(1) ? 0 : 5;
    if (pressed(0)) return 6;
    if (pressed(1)) return 7;
    return 7;   // the original returns with the register unchanged here
}

u32 depthSpeed(u32 scaleAddr, u16 factor) { return u32(u16(0x100 - R16(scaleAddr))) * factor; }

// Moves a 16.16 coordinate by a velocity and stops it on its goal.
// Returns true once the axis has arrived.
bool advanceX(u32 pos, u32 goal, u32 vel) {
    const s16 x = RS16(pos), g = RS16(goal);
    if (RS16(vel) >= 0) {
        if (x >= g) { W16(pos, u16(g)); W16(vel, 0); }
    } else if (x <= g) {
        W16(vel, 0);
    }
    return true;
}

// Addition for mouse play: is the pointer on the panel's swap button? It sits
// at the right of the verb panel and at the left of the inventory panel.
bool overPanelButton() {
    if (!(M.mouseEnabled && M.mouseActive) || M.mouseY < 0xA8) return false;
    return BTST(EngineFlags, 4) ? M.mouseX < 48 : M.mouseX >= 212;
}
bool panelButtonClicked() { return overPanelButton() && edge(4); }

// 0x5E4A. The original is one long routine with many cross jumps; the labels
// below carry the addresses they correspond to.
void control() {
    u16 maxX = 0, maxY = 0;   // D2, D3: room bounds in pixels
    u32 step = 0;             // D0 in the direct-control section
    auto bounds = [&] { maxX = u16((R16(0xFF06C6) << 3) - 1); maxY = u16((R16(0xFF06C8) << 3) - 1); };
    auto turnOrStep = [&](int tableOff, int flag) {   // shared shape of the four direction handlers
        (void)tableOff; (void)flag;
    };
    (void)turnOrStep;

    // --- lead following a scripted path (0x5E4A) ---
    if (BTST(0xFF0AC0, 0)) {
        W8(0xFF0AA5, u8(R8(0xFF0AA5) - 1));
        if (RS8(0xFF0AA5) < 0) {
            W8(0xFF0AA5, R8(0xFF0AAB));
            const u32 path = R32(0xFF05F4);
            for (;;) {
                const u16 at = R16(0xFF060C);
                W16(0xFF060C, u16(at + 4));
                const u16 a = R16(path + s16(at)), b = R16(path + s16(at) + 2);
                if (s16(a) >= 0) { W16(0xFF04DC, a); W16(0xFF04F4, b); break; }
                if (a == 0xFFFD) { W16(0xFF0488, b); BSET(0xFF0AB5, 0); continue; }
                if (a == 0xFFFC || a == 0xFFFB) {
                    if (b != R16(0xFF05A0)) { W16(0xFF05A0, b); W16(0xFF05AC, b); BSET(0xFF0ABE, 0); }
                    if (a == 0xFFFB) continue;
                    break;
                }
                if (a == 0xFFFE) break;
                W16(0xFF060C, 0);
            }
        }
    }

    // --- panel slide between verbs and inventory (0x5EEA) ---
    {
        bool slide = RS8(0xFF0ACB) >= 0;
        if (!slide) {
            if (BTST(0xFF09DE, 2)) { if (BTST(0xFF0AB7, 0)) goto L632C; goto L6D24; }
            if (BTST(0xFF2A00, 1)) goto L632C;
            if (edge(5) || panelButtonClicked()) {
                if (!BTST(0xFF09DE, 2) && !BTST(0xFF2A00, 1)) panelArt(0x3034C);
                W8(0xFF0ACB, 0x10);
                slide = true;
            }
        }
        if (slide) {
            W16(0xFF0800, u16(R16(0xFF0800) + (BTST(EngineFlags, 4) ? 0x10 : -0x10)));
            W8(0xFF0ACB, u8(R8(0xFF0ACB) - 1));
            if (R8(0xFF0ACB) == 0) {
                if (!BTST(0xFF09DE, 2) && !BTST(0xFF2A00, 1)) panelArt(0x3004C);
                W8(0xFF0ACB, 0xFF);
                BCHG(EngineFlags, 4);
            }
        }
    }

    // --- pointer mode (0x600C) ---
    if (!BTST(EngineFlags, 3)) goto L632C;
    if (BTST(0xFF0AC0, 0) && pathEnded(0)) { BCLR(0xFF0AC0, 0); BCLR(0xFF0AC3, 0); }
    if (edge(6)) {
        if (!(M.mouseEnabled && M.mouseActive)) goto L62E8;
        // Addition: right click on a hotspot runs the verb the game suggests
        // for it (the object's default verb).
        if (R16(0xFF06AE) != 0 && !BTST(EngineFlags, 7) && !BTST(0xFF09DE, 0)) {
            const u16 obj = R16(0xFF06AE);
            const s16 verb = obj < 3 ? 10 : RS8(vm::objectAddr(obj) + 0x16);
            if (verb != 0) {
                W16(0xFF06BE, u16(verb));
                BSET(EngineFlags, 7);
                goto L6D24;
            }
        }
        // Otherwise cancel drops the chosen verb but keeps the pointer.
        W16(0xFF06C2, R16(0xFF06C0));
        ui::highlightVerb(0);
        W16(0xFF06C0, 0);
        BCLR(0xFF09DE, 0);
        W16(0xFF06B6, 0);
        goto L6D24;
    }
    if (RS16(CursorY) >= 0xA8) {
        W16(0xFF06E2, 3);
        W16(0xFF06E0, (!BTST(PadPrev, 2) || !BTST(PadPrev, 3) || (M.mouseEnabled && M.mouseActive)) ? 2 : 0x28);
    } else {
        W16(0xFF06E0, 2);
        W16(0xFF06E2, 2);
    }
    if (!BTST(EngineFlags, 7) && !BTST(0xFF0ABF, 0) && !BTST(0xFF09E8, 2) && !BTST(0xFF09E9, 4) && !BTST(0xFF09E9, 6)) {
        W16(0xFF0488, u16(R16(0xFF05B8) + 4));
        BSET(0xFF0AB5, 0);
    }
    if (pressed(2)) { W16(CursorX, u16(R16(CursorX) - R16(0xFF06E0))); if (RS16(CursorX) < -8) W16(CursorX, 0xFFF8); }
    if (pressed(3)) { W16(CursorX, u16(R16(CursorX) + R16(0xFF06E0))); if (RS16(CursorX) > 0xF7) W16(CursorX, 0xF7); }
    if (pressed(0)) {
        if (RS16(CursorY) >= 0xC0) W16(CursorY, 0xBF);
        else { W16(CursorY, u16(R16(CursorY) - R16(0xFF06E2))); if (RS16(CursorY) < 0) W16(CursorY, 0); }
    }
    if (pressed(1)) {
        if (RS16(CursorY) >= 0xA8 && BTST(PadPrev, 1)) W16(CursorY, 0xC0);
        W16(CursorY, u16(R16(CursorY) + R16(0xFF06E2)));
        if (RS16(CursorY) > 0xCF) W16(CursorY, 0xCF);
    }
    // The pointer device places the pointer directly (an addition to the original).
    if (M.mouseEnabled && (M.mouseMoved || M.mouseActive)) {
        int mx = M.mouseX - 8, my = M.mouseY - 8;
        if (mx < -8) mx = -8;
        if (mx > 0xF7) mx = 0xF7;
        if (my < 0) my = 0;
        if (my > 0xCF) my = 0xCF;
        W16(CursorX, u16(mx));
        W16(CursorY, u16(my));
        W16(0xFF06E0, 2);
        M.mouseMoved = false;   // consumed
    }
    if (RS16(CursorY) >= 0xA8) {
        const bool inv = BTST(EngineFlags, 4);
        s16 cell = 1;
        s16 t = RS16(CursorX);
        if (!inv) t = s16(t - 8);
        while ((t = s16(t - 0x28)) >= 0) cell++;
        if (cell > 6) cell = 6;
        if (RS16(CursorY) > 0xC0) cell = s16(cell + 6);
        if (!inv) {
            if (cell > 5) { cell--; if (cell > 0x0A) cell--; }
        } else {
            if (cell > 7) cell--;
            cell--;
        }
        if (overPanelButton()) cell = 0;   // the swap button is not a verb or item cell
        W16(0xFF06EA, u16(cell));
        if (!inv || cell == 5 || cell == 0x0A || cell == 0) goto L6D24;
    }
    if (!edge(4)) goto L6D24;
    // Addition: with a pointer device and no verb chosen, a click in the room
    // walks there, on a hotspot or not, like pressing the walk button would.
    // It never fires the verb the game merely suggests for the hotspot.
    if (M.mouseEnabled && M.mouseActive && R16(0xFF06C0) == 0 && !BTST(0xFF09DE, 0) && RS16(CursorY) < 0xA8 &&
        !BTST(EngineFlags, 7)) {
        M.walkRequest = true;
        M.walkObject = R16(0xFF06AE);
        M.walkX = RS16(CursorX) + 8 + RS16(CameraX);
        M.walkY = RS16(CursorY) - 8 + RS16(CameraY);
        goto L6D24;
    }
    W16(0xFF06BE, R16(0xFF06C0));
    if (R16(0xFF06BE) == 0) {
        W16(0xFF06BE, R16(0xFF06C4));
        if (R16(0xFF06BE) == 0) goto L6D24;
    }
    if (R16(0xFF06AE) != 0) { BSET(EngineFlags, 7); goto L6D24; }
    if (BTST(EngineFlags, 7)) goto L6D24;
L62E8:
    BCLR(EngineFlags, 3);
    W16(0xFF06C2, R16(0xFF06C0));
    ui::highlightVerb(0);
    if (R16(0xFF06AE) != 0) {
        W16(0xFF06C2, u16(s16(RS8(vm::objectAddr(R16(0xFF06AE)) + 0x16))));
        ui::highlightVerb(0);
    }
    goto L6D24;

    // --- direct control of the lead (0x632C) ---
L632C:
    if (BTST(0xFF0AC0, 0)) {
        bounds();
        if (pathEnded(0)) { BCLR(0xFF0AC0, 0); BCLR(0xFF0AC3, 0); }
        goto L6B28;
    }
    if (BTST(0xFF09E8, 2)) {
        if (!BTST(0xFF09EA, 0)) goto L6C28;
        bounds();
        goto L6B28;
    }
    if (BTST(0xFF0ABF, 0)) goto L6D24;
    if (BTST(0xFF09E9, 4)) goto L6C28;
    if (BTST(EngineFlags, 7) && !BTST(0xFF0AB7, 0)) goto L6D24;
    W32(0xFF0642, R32(0xFF04DC));
    W32(0xFF0646, R32(0xFF04F4));
    {
        const u32 sp = depthSpeed(0xFF05A0, 0x155);
        W32(0xFF062A, sp);
        const s16 unit = s16(s32(sp) >> 8);
        if (!BTST(0xFF0AB7, 0)) { W16(0xFF062E, 0x100); W16(0xFF0630, 0x100); }
        W32(0xFF0632, u32(s32(unit) * RS16(0xFF062E)));
        W32(0xFF0636, u32(s32(unit) * RS16(0xFF0630)));
    }
    bounds();
    if (BTST(EngineFlags, 7)) { if (BTST(0xFF0AB7, 0)) goto L6960; goto L6AE8; }
    step = R32(0xFF062A);
    if ((R8(PadState) & 0x0F) == 0x0F) {
        if (BTST(0xFF0ACC, 1)) goto L656C;
        if (BTST(0xFF0ACC, 0)) goto L660C;
        step >>= 1;
        if (BTST(0xFF0ACC, 2)) goto L66E2;
        if (BTST(0xFF0ACC, 3)) goto L6790;
        if (BTST(0xFF0AB7, 0)) goto L6960;
        W16(0xFF0686, u16(R16(0xFF0686) - 1));
        if (RS16(0xFF0686) < 0) {   // idle fidget every five seconds
            W16(0xFF0686, 0x12C);
            W16(0xFF0488, u16(0x20 + R16(0xFF05B8)));
            BSET(0xFF0AB5, 0);
            goto L681A;
        }
        if (RS16(0xFF0488) >= 0x20 && RS16(0xFF0488) < 0x24 && !BTST(0xFF0ABC, 0)) goto L681A;
        const u16 idle = u16(R16(0xFF05B8) + 4);
        if (idle != R16(0xFF0488) && !BTST(0xFF0AB5, 0)) { W16(0xFF0488, idle); BSET(0xFF0AB5, 0); }
        goto L681A;
    }
    W16(0xFF0686, 0x12C);
    if (!pressed(2)) goto L65E4;
L656C:
    if (R16(0xFF0488) != 1) {
        const u16 turn = R16(0xFC720 + (R16(0xFF05B8) << 3) + 2);
        step >>= 1;
        if (turn != R16(0xFF0488)) { W16(0xFF0488, turn); BSET(0xFF0AB5, 0); BSET(0xFF0ACC, 1); goto L6682; }
    } else {
        W16(0xFF05B8, 1);
        if (!BTST(0xFF0ABD, 0)) goto L6682;
        BCLR(0xFF0ACC, 1);
    }
    W32(0xFF0642, R32(0xFF0642) - step);
    if (s32(R32(0xFF0642)) < 0) W32(0xFF0642, 0);
    goto L6682;
L65E4:
    if (BTST(0xFF0ACC, 1) && RS16(0xFF0488) <= 3) BCLR(0xFF0ACC, 1);
    if (!pressed(3)) goto L6682;
L660C:
    if (R16(0xFF0488) != 0) {
        const u16 turn = R16(0xFC720 + (R16(0xFF05B8) << 3));
        step >>= 1;
        if (turn != R16(0xFF0488)) { W16(0xFF0488, turn); BSET(0xFF0AB5, 0); BSET(0xFF0ACC, 0); goto L669E; }
    } else {
        W16(0xFF05B8, 0);
        if (!BTST(0xFF0ABD, 0)) goto L669E;
        BCLR(0xFF0ACC, 0);
    }
    W32(0xFF0642, R32(0xFF0642) + step);
    if (s16(maxX) < RS16(0xFF0642)) W16(0xFF0642, maxX);
    goto L669E;
L6682:
    if (BTST(0xFF0ACC, 0) && RS16(0xFF0488) <= 3) BCLR(0xFF0ACC, 0);
L669E:
    step = R32(0xFF062A);
    if (BTST(0xFF0ACC, 1) || BTST(0xFF0ACC, 0)) goto L681A;
    step >>= 1;
    if (!pressed(0)) goto L676C;
    if (pressed(2) || pressed(3)) goto L6726;
L66E2:
    if (R16(0xFF0488) != 2) {
        const u16 turn = R16(0xFC720 + (R16(0xFF05B8) << 3) + 4);
        step >>= 1;
        if (turn != R16(0xFF0488)) { W16(0xFF0488, turn); BSET(0xFF0AB5, 0); BSET(0xFF0ACC, 2); goto L681A; }
        goto L674A;
    }
L6726:
    W16(0xFF05B8, 2);
    if (!BTST(0xFF0ABD, 0)) goto L676C;
    BCLR(0xFF0ACC, 2);
    BCLR(0xFF0ACC, 3);
L674A:
    if (BTST(0xFF09E9, 0)) goto L6804;
L6756:
    W32(0xFF0646, R32(0xFF0646) - step);
    if (s32(R32(0xFF0646)) < 0) W16(0xFF0646, 0);
    goto L681A;
L676C:
    if (!pressed(1)) goto L681A;
    if (pressed(2) || pressed(3)) goto L67D4;
L6790:
    if (R16(0xFF0488) != 3) {
        const u16 turn = R16(0xFC720 + (R16(0xFF05B8) << 3) + 6);
        step >>= 1;
        if (turn != R16(0xFF0488)) { W16(0xFF0488, turn); BSET(0xFF0AB5, 0); BSET(0xFF0ACC, 3); goto L681A; }
        goto L67F8;
    }
L67D4:
    W16(0xFF05B8, 3);
    if (!BTST(0xFF0ABD, 0)) goto L681A;
    BCLR(0xFF0ACC, 3);
    BCLR(0xFF0ACC, 2);
L67F8:
    if (BTST(0xFF09E9, 0)) goto L6756;
L6804:
    W32(0xFF0646, R32(0xFF0646) + step);
    if (s16(maxY) < RS16(0xFF0646)) W16(0xFF0646, maxY);
L681A:
    BCLR(0xFF0AB7, 0);
    if (!room::blockedTrial(maxX, maxY)) {
        W32(0xFF04DC, R32(0xFF0642));
        W32(0xFF04F4, R32(0xFF0646));
        goto L6AE8;
    }
    {   // blocked: try to slide sideways around the obstacle
        const int dir = padDirection();
        if (dir <= 3) goto L6AE8;
        const bool vertical = dir > 5;
        const u32 axis = vertical ? 0xFF0642 : 0xFF0646;
        const int tries = vertical ? 3 : 4;
        s16 vel = -0x100;
        bool found = false;
        for (int k = 0; k < tries && !found; k++) { W16(axis, u16(R16(axis) - 1)); found = !room::blockedTrial(maxX, maxY); }
        if (!found) {
            W16(axis, u16(R16(axis) + tries));
            vel = 0x100;
            for (int k = 0; k < tries && !found; k++) { W16(axis, u16(R16(axis) + 1)); found = !room::blockedTrial(maxX, maxY); }
        }
        if (!found) {
            if (!vertical) W16(0xFF0642, u16(R16(0xFF0642) + 1));
            goto L6AE8;
        }
        if (vertical) {
            W16(0xFF062E, u16(vel)); W16(0xFF0630, 0);
            W16(0xFF05DC, R16(0xFF0642));
            BSET(0xFF0AB7, 0);
            W32(0xFF04F4, R32(0xFF0646));
            W16(0xFF05E8, R16(0xFF0646));
        } else {
            W16(0xFF0630, u16(vel)); W16(0xFF062E, 0);
            W32(0xFF04DC, R32(0xFF0642));
            W16(0xFF05DC, R16(0xFF0642));
            BSET(0xFF0AB7, 0);
            W16(0xFF05E8, R16(0xFF0646));
        }
    }
L6960:
    W16(0xFF0686, 0x12C);
    W32(0xFF04DC, R32(0xFF04DC) + u32(s32(s16(s32(depthSpeed(0xFF05A0, 0x155)) >> 8)) * RS16(0xFF062E)));
    W32(0xFF04F4, R32(0xFF04F4) + u32(s32(s16(s32(depthSpeed(0xFF05AC, 0xAA)) >> 8)) * RS16(0xFF0630)));
    advanceX(0xFF04DC, 0xFF05DC, 0xFF062E);
    {
        const s16 y = RS16(0xFF04F4), g = RS16(0xFF05E8);
        if (RS16(0xFF0630) >= 0) {
            if (y < g) goto L6AE8;
            W16(0xFF04F4, u16(g));
            W16(0xFF0630, 0);
        } else if (y <= g) {
            W16(0xFF04F4, u16(g));
            W16(0xFF0630, 0);
        }
    }
    if (R16(0xFF062E) != 0 || R16(0xFF0630) != 0) goto L6AE8;
    if (RS16(0xFF065A) >= 0) {
        W16(0xFF065A, u16(R16(0xFF065A) - 1));
        if (RS16(0xFF065A) >= 0) {   // next leg of the route
            const u32 a = 0xFF065C + R16(0xFF065A) * 2;
            W16(0xFF05DC, R16(a)); W16(0xFF05E8, R16(a + 4));
            W16(0xFF062E, R16(a + 8)); W16(0xFF0630, R16(a + 0x0C));
            const u16 facing = R16(a + 0x10);
            const u16 rel = u16(R16(0xFF0488) - R16(0xFF05B8));
            W16(0xFF0488, rel == 0 ? R16(0xFC720 + (R16(0xFF05B8) << 3) + facing * 2) : u16(rel + facing));
            W16(0xFF05B8, facing);
            BSET(0xFF0AB5, 0);
            goto L6AE8;
        }
    }
    BCLR(0xFF0AB7, 0);
    if (BTST(EngineFlags, 7)) { W16(0xFF0488, u16(R16(0xFF05B8) + 4)); BSET(0xFF0AB5, 0); }
L6AE8:
    priorityFromMap(0, R16(0xFF04DC), R16(0xFF04F4));

    // --- camera follows the lead (0x6B28) ---
L6B28:
    if (!BTST(0xFF09E8, 1)) {
        const s16 limX = s16(maxX - 0xFF), limY = s16(maxY - 0x97);
        s16 d = s16(R16(0xFF04DC) - R16(CameraX));
        if (d <= 0x4B || d >= 0xB5) {
            s16 cx;
            if (d <= 0x4B) { cx = s16(R16(0xFF04DC) - 0x4B); if (cx < 0) cx = 0; }
            else { cx = s16(R16(0xFF04DC) - 0xB4); if (cx > limX) cx = limX; }
            const u16 was = R16(CameraX) >> 3, now = u16(cx) >> 3;
            if (was != now) BSET(0xFF0AB3, s16(was) < s16(now) ? 0 : 1);
            W16(CameraX, u16(cx));
        }
        d = s16(R16(0xFF04F4) - R16(CameraY));
        if (d <= 0x32 || d >= 0x66) {
            s16 cy;
            if (d <= 0x32) { cy = s16(R16(0xFF04F4) - 0x32); if (cy < 0) cy = 0; }
            else { cy = s16(R16(0xFF04F4) - 0x65); if (cy > limY) cy = limY; }
            const u16 was = R16(CameraY) >> 3, now = u16(cy) >> 3;
            if (was != now) BSET(0xFF0AB3, s16(was) < s16(now) ? 3 : 2);
            W16(CameraY, u16(cy));
        }
    }

    // --- entering pointer mode (0x6C28) ---
L6C28:
    if (BTST(EngineFlags, 7) || BTST(0xFF2A00, 1)) goto L6D24;
    if (!(edge(6) || edge(4) || (M.mouseEnabled && M.mouseActive))) goto L6D24;
    BSET(EngineFlags, 3);
    BCLR(0xFF09DE, 0);
    W16(0xFF06E0, 2); W16(0xFF06E2, 2);
    W16(0xFF06C0, 0); W16(0xFF06B4, 0); W16(0xFF06B6, 0);
    if (BTST(0xFF09E9, 6) || (!BTST(0xFF0ABF, 0) && BTST(0xFF09E9, 4))) {
        W16(CursorX, 0x80);
        W16(CursorY, 0x50);
    } else {
        if (!BTST(0xFF0ABF, 0) && !BTST(0xFF09E8, 2)) { W16(0xFF0488, u16(R16(0xFF05B8) + 4)); BSET(0xFF0AB5, 0); }
        W16(CursorX, u16(R16(0xFF04DC) - R16(CameraX)));
        W16(CursorY, u16(R16(0xFF04F4) - R16(CameraY) + 0x10 - 0x3C));
    }

    // --- the companion (0x6D24) ---
L6D24:
    if (BTST(0xFF09EB, 2)) goto L707E;
    if (BTST(0xFF09EB, 6)) {   // glued to the lead
        W16(0xFF04E0, R16(0xFF04DC));
        W16(0xFF04F8, R16(0xFF04F4));
        u16 a = R16(0xFF05B8);
        if ((R8(PadState) & 0x0F) == 0x0F || BTST(EngineFlags, 3)) a = u16(a + 4);
        if (a != R16(0xFF048A)) { W16(0xFF048A, a); BSET(0xFF0AB5, 1); }
        goto L707E;
    }
    if (BTST(0xFF0AC0, 1)) {   // scripted path
        W8(0xFF0AA6, u8(R8(0xFF0AA6) - 1));
        if (RS8(0xFF0AA6) >= 0) goto L707E;
        W8(0xFF0AA6, R8(0xFF0AAC));
        const u32 path = R32(0xFF05F8);
        for (;;) {
            const u16 at = R16(0xFF060E);
            W16(0xFF060E, u16(at + 4));
            const u16 a = R16(path + s16(at)), b = R16(path + s16(at) + 2);
            if (s16(a) >= 0) { W16(0xFF04E0, a); W16(0xFF04F8, b); break; }
            if (a == 0xFFFD) { W16(0xFF048A, b); BSET(0xFF0AB5, 1); continue; }
            W16(0xFF060E, 0);
            if (a == 0xFFFE || BTST(0xFF0AC3, 1)) { BCLR(0xFF0AC0, 1); BCLR(0xFF0AC3, 1); break; }
        }
        goto L707E;
    }
    if (!BTST(0xFF0AB7, 1)) {
        if (RS16(0xFF0688) >= 0) W16(0xFF0688, u16(R16(0xFF0688) - 1));
        goto L707E;
    }
    if (BTST(0xFF09DE, 5)) {   // arrived: settle into the idle pose
        if (BTST(0xFF0AB5, 1) || !BTST(0xFF0ABC, 1)) goto L707E;
        W16(0xFF048A, u16(R16(0xFF063E) + R16(0xFF0640)));
        W16(0xFF05BA, R16(0xFF0640));
        W16(0xFF061A, 0xFFFF);
        BSET(0xFF0AB5, 1);
        BCLR(0xFF0AB7, 1);
        BCLR(0xFF09DE, 5);
        goto L707E;
    }
    W32(0xFF04E0, R32(0xFF04E0) + u32(s32(s16(s32(depthSpeed(0xFF05A2, 0x12A)) >> 8)) * RS16(0xFF063A)));
    W32(0xFF04F8, R32(0xFF04F8) + u32(s32(s16(s32(depthSpeed(0xFF05AE, 0x95)) >> 8)) * RS16(0xFF063C)));
    advanceX(0xFF04E0, 0xFF05DE, 0xFF063A);
    {
        const s16 y = RS16(0xFF04F8), g = RS16(0xFF05EA);
        if (RS16(0xFF063C) >= 0) {
            if (y < g) goto L707E;
            W16(0xFF04F8, u16(g));
            W16(0xFF063C, 0);
        } else if (y <= g) {
            W16(0xFF04F8, u16(g));
            W16(0xFF063C, 0);
        }
    }
    if (R16(0xFF063A) != 0 || R16(0xFF063C) != 0) goto L707E;
    if (RS16(0xFF0670) >= 0) {
        W16(0xFF0670, u16(R16(0xFF0670) - 1));
        if (RS16(0xFF0670) >= 0) {
            const u32 a = 0xFF0672 + R16(0xFF0670) * 2;
            W16(0xFF05DE, R16(a)); W16(0xFF05EA, R16(a + 4));
            W16(0xFF063A, R16(a + 8)); W16(0xFF063C, R16(a + 0x0C));
            const u16 facing = R16(a + 0x10);
            const u16 rel = u16(R16(0xFF048A) - R16(0xFF05BA));
            W16(0xFF048A, rel == 0 ? R16(0xFC6E0 + (R16(0xFF05BA) << 3) + facing * 2) : u16(rel + facing));
            W16(0xFF05BA, facing);
            BSET(0xFF0AB5, 1);
            W16(0xFF061A, 0xFFFF);
            goto L707E;
        }
    }
    if (BTST(EngineFlags, 7)) {
        W16(0xFF048A, u16(R16(0xFF05BA) + 4));
        BSET(0xFF0AB5, 1);
        BCLR(0xFF0AB7, 1);
    } else {
        BSET(0xFF09DE, 5);
        W16(0xFF048A, R16(0xFC700 + (R16(0xFF05BA) << 3) + R16(0xFF0640) * 2));
        W16(0xFF061A, 0xFFFF);
        BSET(0xFF0AB5, 1);
    }

L707E:
    priorityFromMap(1, R16(0xFF04E0), R16(0xFF04F8));
    if (BTST(0xFF09E9, 3)) {   // riding together
        W8(0xFF0ABB, u8((R8(0xFF0ABB) & 0xF0) | 3));
        W32(0xFF04E4, R32(0xFF04DC));
        W32(0xFF04FC, R32(0xFF04F4));
        const bool far = RS16(0xFF04DC) >= 0xF3;
        W16(0xFF05B0, far ? 0 : 0x80);
        W16(0xFF05A4, far ? 0x80 : 0);
        W16(0xFF04FC, u16(R16(0xFF04FC) - 0x14));
        W16(0xFF04E4, u16(R16(0xFF04E4) + 0x14));
        if (RS16(0xFF04E4) > 0x160) W16(0xFF04E4, 0x160);
    }

    // --- depth scaling from the room's scale table (0x715A) ---
    for (int i = 5; i >= 0; i--) {
        if (!BTST(0xFF0AB8, i) || BTST(0xFF09E9, 1)) continue;
        const u16 y = R16(0xFF04F4 + i * 4);
        const s16 row = s16(y >> 3);
        u32 p = R32(0xFF0692);
        s16 lowVal = 0, lowRow = 0, r = 0;
        do {
            const s16 v = RS16(p); p += 2;
            if (v >= 0) { lowVal = v; lowRow = r; }
            r++;
        } while (r < row);
        s16 highVal = 0, highRow = RS16(0xFF06C8);
        while (r < RS16(0xFF06C8)) {
            const s16 v = RS16(p); p += 2;
            if (v >= 0) { highVal = v; highRow = r; break; }
            r++;
        }
        const s16 dist = s16(highRow * 8 + 7 - s16(y));
        const s16 span = s16((highRow - lowRow) << 3);
        const s16 delta = s16((highVal - lowVal) << 4);
        const s16 scale = s16((highVal << 4) - (span ? s16(s32(delta) * dist / span) : 0));
        if (R16(0xFF05A0 + i * 2) != u16(scale)) { W16(0xFF05A0 + i * 2, u16(scale)); BSET(0xFF0ABE, i); }
        if (R16(0xFF05AC + i * 2) != u16(scale)) { W16(0xFF05AC + i * 2, u16(scale)); BSET(0xFF0ABE, i); }
    }
    if (BTST(0xFF09E8, 3)) {
        const s16 p = RS16(0xFF08B0);
        W16(0xFF05A0 + p * 2, R16(0xFF05A0));
        W16(0xFF05AC + p * 2, R16(0xFF05AC));
    }

    // --- the other actors (0x723A) ---
    {
        u32 pos = 0xFF04E4, goal = 0xFF05E0;   // advanced as the original advances A0 and A1
        for (int i = 2; i < 6; i++, pos += 4, goal += 2) {
            if (BTST(0xFF09E9, 3) && i < 4) continue;
            if (!BTST(0xFF0AB4, i)) continue;
            if (BTST(0xFF0AB7, i)) {
                u32 v = R32(pos + 0x38);
                s16 g = RS16(goal);
                if (g != RS16(pos)) {
                    if (g > RS16(pos)) { W32(pos, R32(pos) + v); if (s32(R32(goal)) <= s32(R32(pos))) W32(pos, R32(goal)); }
                    else { W32(pos, R32(pos) - v); if (s32(R32(goal)) >= s32(R32(pos))) W32(pos, R32(goal)); }
                }
                g = RS16(goal + 0x0C);
                v = R32(pos + 0x4C);
                if (g != RS16(pos + 0x18)) {
                    if (g > RS16(pos + 0x18)) { W32(pos + 0x18, R32(pos + 0x18) + v); if (s32(R32(goal + 0x0C)) <= s32(R32(pos + 0x18))) W32(pos + 0x18, R32(goal + 0x0C)); }
                    else { W32(pos + 0x18, R32(pos + 0x18) - v); if (s32(R32(goal + 0x0C)) >= s32(R32(pos + 0x18))) W32(pos + 0x18, R32(goal + 0x0C)); }
                }
            } else if (BTST(0xFF0AC0, i)) {
                W8(0xFF0AA5 + i, u8(R8(0xFF0AA5 + i) - 1));
                if (RS8(0xFF0AA5 + i) < 0) {
                    W8(0xFF0AA5 + i, R8(0xFF0AAB + i));
                    const u32 path = R32(0xFF05F4 + i * 4);
                    pos = path;   // the original reuses the position register here
                    for (;;) {
                        const u16 at = R16(0xFF060C + i * 2);
                        W16(0xFF060C + i * 2, u16(at + 4));
                        const u16 a = R16(path + s16(at)), b = R16(path + s16(at) + 2);
                        if (s16(a) >= 0) { W16(0xFF04DC + i * 4, a); W16(0xFF04F4 + i * 4, b); break; }
                        if (a == 0xFFFD) { W16(0xFF0488 + i * 2, b); BSET(0xFF0AB5, i); continue; }
                        W16(0xFF060C + i * 2, 0);
                        if (a == 0xFFFE || BTST(0xFF0AC3, i)) { BCLR(0xFF0AC0, i); BCLR(0xFF0AC3, i); break; }
                    }
                }
            }
            if (R16(pos) == R16(goal) && R16(pos + 0x18) == R16(goal + 0x0C)) BCLR(0xFF0AB7, i);
            if (!BTST(0xFF0AC2, i)) priorityFromMap(i, R16(pos), R16(pos + 0x18));
        }
    }
}

}  // namespace

void gameFrameWork() {
    BCLR(EngineFlags, 6);
    paletteCycles();
    ui::inventoryFrameWork();
    uploadActorTiles();
    buildSprites();
    ll::dmaVram(SpriteBuffer, 0xD800, 0xC8);
    for (int i = 0; i < 6; i++)
        if (RS16(0xFF0618 + i * 2) > 0) W16(0xFF0618 + i * 2, u16(R16(0xFF0618 + i * 2) - 1));
    pageArrowIcons();
    if (BTST(EngineFlags, 2)) room::objectPass();
    control();
    BSET(EngineFlags, 6);
}

}  // namespace irq
