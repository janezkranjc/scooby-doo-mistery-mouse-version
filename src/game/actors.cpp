#include "actors.h"
#include "addr.h"
#include "room.h"
#include "scaler.h"
#include "script.h"
#include "sound.h"

namespace actor {

namespace {

// Per-actor arrays, indexed 0..5. Actors 0 and 1 are the two lead characters.
constexpr u32 kBase = 0xFF0470;        // long: animation data
constexpr u32 kAnim = 0xFF0488;        // word: animation number
constexpr u32 kAnimPc = 0xFF0494;      // word: offset of the next animation command
constexpr u32 kFrame = 0xFF04A0;       // word: offset of the current frame header
constexpr u32 kOriginX = 0xFF04AC;     // word: scaled frame origin
constexpr u32 kOriginY = 0xFF04B8;
constexpr u32 kPosX = 0xFF04DC;        // long 16.16
constexpr u32 kPosY = 0xFF04F4;
constexpr u32 kDelay = 0xFF050C;       // word: frame delay reload
constexpr u32 kTiles = 0xFF0540;       // long: tile data waiting for upload
constexpr u32 kHeader = 0xFF0570;      // long: frame header of the pending frame
constexpr u32 kUploadLen = 0xFF0588;   // word
constexpr u32 kScaleX = 0xFF05A0;      // word: 0 full size .. 255
constexpr u32 kScaleY = 0xFF05AC;
constexpr u32 kTimer = 0xFF0618;       // word: frames until the next command
constexpr u32 kBuffers = 0xFC9F4;      // long: per-actor tile buffer (ROM table)

constexpr u32 kActive = 0xFF0AB4, kRequest = 0xFF0AB5, kUpload = 0xFF0AB6, kWalking = 0xFF0AB7;
constexpr u32 kScalable = 0xFF0AB8, kScalePending = 0xFF0AB9, kFinished = 0xFF0ABC, kShown = 0xFF0ABD;
constexpr u32 kForce = 0xFF0ABE, kPairReady = 0xFF0AC6, kLoopBreak = 0xFF0AC3;

u16 uploadLength(u32 header) {
    switch (R8(header + 4)) {
        case 0x70: return 0x700;
        case 0x40: return 0x400;
        case 0x20: return R8(header + 5) == 0x50 ? 0x280 : 0x200;
        default: return 0x3C0;
    }
}

void showFrame(int i, u16 frameOffset) {
    const u32 base = R32(kBase + i * 4);
    const u32 header = base + s16(frameOffset);
    const u32 data = R32(header + 6) + base;
    W16(kUploadLen + i * 2, uploadLength(header));
    if (BTST(kScalable, i)) {
        if (BTST(kUpload, i)) return;
        W32(kTiles + i * 4, data);
        BSET(kScalePending, i);
    } else {
        const u32 buffer = R32(kBuffers + i * 4);
        W32(kTiles + i * 4, buffer);
        ll::packbitsDecode(data, buffer);
        BSET(kUpload, i);
    }
    W32(kHeader + i * 4, header);
    W16(kTimer + i * 2, R16(kDelay + i * 2));
}

void stepAnimation(int i) {
    if (!BTST(kActive, i) || RS16(kTimer + i * 2) > 0) return;
    if (BTST(0xFF09E8, 3) && BTST(kPairReady, i)) return;
    if (!BTST(kRequest, i) && !BTST(kForce, i) && i != 1) {
        const s16 x = s16(R16(kPosX + i * 4) - R16(CameraX));
        const s16 y = s16(R16(kPosY + i * 4) - R16(CameraY));
        if (x < -0x50 || x > 0x150 || y < -0x50 || y > 0xE8) return;
    }
    const u32 base = R32(kBase + i * 4);
    if (BTST(kRequest, i)) {
        if (BTST(kUpload, i) || BTST(kScalePending, i)) return;
        BCLR(kFinished, i);
        BCLR(kShown, i);
        const u16 entry = u16(R16(base + 2) + R16(kAnim + i * 2) * 2);
        W16(kAnimPc + i * 2, R16(base + s16(entry)));
        W16(kTimer + i * 2, 0xFFFF);
        BCLR(kRequest, i);
    }
    if (BTST(kFinished, i)) {
        if (!BTST(kForce, i)) return;
        BCLR(kForce, i);
        showFrame(i, R16(kFrame + i * 2));
        return;
    }
    for (;;) {
        const u16 at = R16(kAnimPc + i * 2);
        W16(kAnimPc + i * 2, u16(at + 4));
        const u16 cmd = R16(base + s16(at));
        u16 arg = R16(base + s16(at) + 2);
        switch (cmd) {
            case 4: W16(kDelay + i * 2, arg); break;
            case 5: W16(kTimer + i * 2, arg); return;
            case 3:
                if (BTST(kLoopBreak, i)) {
                    BCLR(kLoopBreak, i);
                    BCLR(kShown, i);
                    W16(kAnim + i * 2, arg);
                } else {
                    W16(kAnimPc + i * 2, arg);
                }
                break;
            case 2:
                W16(kFrame + i * 2, arg);
                if (i == 0 || i == RS16(0xFF08B0)) BSET(kPairReady, i);
                showFrame(i, arg);
                return;
            case 9: BSET(kFinished, i); return;
            case 0x0D:
                BCLR(kShown, i);
                W16(kAnim + i * 2, arg);
                break;
            case 6:
                W16(kPosX + i * 4, u16(R16(kPosX + i * 4) + s16(s8(arg))));
                W16(kPosY + i * 4, u16(R16(kPosY + i * 4) + (s16(arg) >> 8)));
                break;
            case 0x11:
                if (s16(arg) >= 0) snd::startSequence(arg);
                else snd::stopSequence(arg & 0x7FFF);
                break;
            default: break;
        }
    }
}

bool pairHeld(int i) { return BTST(0xFF09E8, 3) && (i == 0 || i == RS16(0xFF08B0)); }

void scalePending(int i) {
    if (!BTST(kScalePending, i)) return;
    const int partner = R16(0xFF08B0) & 7;
    if (pairHeld(i) && !(BTST(kPairReady, 0) && BTST(kPairReady, partner))) return;
    const u32 base = R32(kBase + i * 4);
    const u32 leadFrame = R32(kBase) + s16(R16(kFrame));
    const u32 dst = (i == 0 && R8(leadFrame + 4) == 0x70) ? 0xFF5000 : 0xFF6000;
    ll::packbitsDecode(R32(kTiles + i * 4), dst);
    const u16 sx = R16(kScaleX + i * 2), sy = R16(kScaleY + i * 2);
    W16(0xFF0624, sx);
    W16(0xFF0626, sy);
    const u32 header = base + s16(R16(kFrame + i * 2));
    if (BTST(header + 0x0A, 3)) {
        const u16 width = R8(header + 4);
        const u16 d = u16(u16(u32(u16(width - R16(header))) * u16(0x100 - sx)) >> 8);
        W16(kOriginX + i * 2, u16(width - d));
    } else {
        W16(kOriginX + i * 2, u16(u16(u32(R16(header)) * u16(0x100 - sx)) >> 8));
    }
    const u16 oy = u16(u16(u32(R16(header + 2)) * u16(0x100 - sy)) >> 8);
    W16(kOriginY + i * 2, oy);
    const u32 buffer = R32(kBuffers + i * 4);
    const bool tall = R8(header + 4) == 0x30;
    if (BTST(0xFF09E9, 3) && s16(oy) < 2) {
        if (tall) scaler::copyTall(buffer);
        else scaler::copyWide(buffer);
    } else {
        if (tall) scaler::actorTall(buffer);
        else scaler::actorWide(buffer);
    }
    W32(kTiles + i * 4, buffer);
    if (pairHeld(i)) return;
    BSET(kUpload, i);
    BCLR(kScalePending, i);
}

// Addresses that differ between the two lead characters' walk code.
struct Lead {
    int index;
    u32 facing, turnTable;
    u32 goalX, goalY;        // current target
    u32 stepX, stepY;        // per-frame velocity, 8.8
    u32 legCount, legs;      // remaining waypoint legs and their table
};
const Lead kLead{0, 0xFF05B8, 0xFC720, 0xFF05DC, 0xFF05E8, 0xFF062E, 0xFF0630, 0xFF065A, 0xFF065C};
const Lead kCompanion{1, 0xFF05BA, 0xFC6E0, 0xFF05DE, 0xFF05EA, 0xFF063A, 0xFF063C, 0xFF0670, 0xFF0672};

void setOrigin(u16 x, u16 y) {
    W16(0xFF0652, x); W16(0xFF0654, 0);
    W16(0xFF0656, y); W16(0xFF0658, 0);
}
void setOriginActor(int i) {
    W32(0xFF0652, R32(kPosX + i * 4));
    W32(0xFF0656, R32(kPosY + i * 4));
}

u16 lastClearX = 0, lastClearY = 0;   // last free point seen by lineBlocked

// 0x5378. Walks a straight line from the origin to (x, y) one pixel at a
// time along the longer axis and reports whether anything blocks it.
bool lineBlocked(u16 x, u16 y) {
    const s16 dx = s16(x - R16(0xFF0652)), dy = s16(y - R16(0xFF0656));
    const u16 adx = u16(dx < 0 ? -dx : dx), ady = u16(dy < 0 ? -dy : dy);
    s32 sx, sy;
    if (dx == 0) { sx = 0; sy = dy < 0 ? -0x10000 : 0x10000; }
    else if (dy == 0) { sy = 0; sx = dx < 0 ? -0x10000 : 0x10000; }
    else {
        if (s16(ady) <= s16(adx)) { sy = s32(((u32(ady) << 16) / adx) & 0xFFFF); sx = 0x10000; }
        else { sx = s32(((u32(adx) << 16) / ady) & 0xFFFF); sy = 0x10000; }
        if (dx < 0) sx = -sx;
        if (dy < 0) sy = -sy;
    }
    W32(0xFF064A, R32(0xFF0652));
    W32(0xFF064E, R32(0xFF0656));
    for (;;) {
        if (room::blockedProbe()) return true;
        lastClearX = R16(0xFF064A);
        lastClearY = R16(0xFF064E);
        W32(0xFF064A, R32(0xFF064A) + u32(sx));
        W32(0xFF064E, R32(0xFF064E) + u32(sy));
        const s16 cx = RS16(0xFF064A), cy = RS16(0xFF064E);
        if (sx < 0 ? s16(x) < cx : s16(x) > cx) continue;
        if (sy < 0 ? s16(y) < cy : s16(y) > cy) continue;
        return false;
    }
}

// 0x548E / 0x59DC. Velocity from the origin towards (x, y) at `speed`, with
// Y counted double for the facing decision. Returns the facing 0..3.
u16 aim(u16 x, u16 y, u16 speed, s16& vx, s16& vy) {
    const s16 dx = s16(x - R16(0xFF0652));
    const s16 dy = s16(u16(y - R16(0xFF0656)) << 1);
    const u16 adx = u16(dx < 0 ? -dx : dx), ady = u16(dy < 0 ? -dy : dy);
    u16 facing;
    if (s16(ady) >= s16(adx)) facing = dy < 0 ? 2 : 3;
    else facing = dx < 0 ? 1 : 0;
    const u16 unit = u16(speed << 8);
    const u32 ax = u32(adx) << 8, ay = u32(ady) << 8;
    auto div = [](u32 a, u16 b) -> u16 { return b ? u16(a / b) : 0; };
    s16 ux, uy;
    if (ay == 0) { uy = 0; ux = 0x100; }
    else if (ax == 0) { ux = 0; uy = 0x100; }
    else {
        const u16 qx = div(ax, unit), qy = div(ay, unit);
        if (s16(qy) <= s16(qx)) { ux = 0x100; uy = s16(div(ay, qx)); }
        else { uy = 0x100; ux = s16(div(ax, qy)); }
    }
    if (dx < 0) ux = s16(-ux);
    if (dy < 0) uy = s16(-uy);
    vx = s16(ux * s16(speed));
    vy = s16(uy * s16(speed));
    return facing;
}

void gateway(int n, u16& x, u16& y) {
    const u32 g = R32(0xFF069A) + 0x20 + n * 4;
    x = u16(R16(g) << 3);
    y = u16(R16(g + 2) << 3);
}

// Starts a walk for one of the lead characters, through up to two of the
// room's gateway points when the straight line is blocked. Returns the facing.
u16 startWalk(const Lead& L, u16 x, u16 y, u16 speed, u16 anim, bool& started) {
    const int i = L.index;
    started = false;
    BCLR(kWalking, i);
    if (i == 1) BCLR(0xFF09DE, 5);
    if (x == R16(kPosX + i * 4) && y == R16(kPosY + i * 4)) return 0;
    started = true;

    u16 gx[2], gy[2];
    gateway(0, gx[0], gy[0]);
    gateway(1, gx[1], gy[1]);
    auto fromGate = [&](int n) { setOrigin(gx[n], gy[n]); return !lineBlocked(x, y); };
    auto leg = [&](int slot, int from, u16 tx, u16 ty) {   // velocity for a later leg
        setOrigin(gx[from], gy[from]);
        s16 vx, vy;
        const u16 f = aim(tx, ty, speed, vx, vy);
        W16(L.legs + 8 + slot * 2, u16(vx));
        W16(L.legs + 0x0C + slot * 2, u16(vy));
        W16(L.legs + 0x10 + slot * 2, f);
    };
    u16 tx = x, ty = y, legs = 0;
    auto oneGate = [&](int n) {   // lead -> gate n -> target
        W16(L.legs, x); W16(L.legs + 4, y);
        leg(0, n, x, y);
        tx = gx[n]; ty = gy[n]; legs = 1;
    };
    auto twoGates = [&](int first, int second) {   // lead -> first -> second -> target
        W16(L.legs, x); W16(L.legs + 4, y);
        leg(0, second, x, y);
        W16(L.legs + 2, gx[second]); W16(L.legs + 6, gy[second]);
        leg(1, first, gx[second], gy[second]);
        tx = gx[first]; ty = gy[first]; legs = 2;
    };

    bool direct = BTST(0xFF2A00, 4);
    if (!direct) {
        setOriginActor(i);
        direct = !lineBlocked(x, y);
    }
    if (!direct) {
        int mask = 0;
        for (int n = 0; n < 2; n++) {
            setOriginActor(i);
            if (!lineBlocked(gx[n], gy[n])) mask |= 1 << n;
        }
        if (mask == 3) {
            const u32 g = R32(0xFF069A) + 0x20;
            const s32 d0 = s32(RS16(g)) * RS16(g) + s32(RS16(g + 2)) * RS16(g + 2);
            const s32 d1 = s32(RS16(g + 4)) * RS16(g + 4) + s32(RS16(g + 6)) * RS16(g + 6);
            const int n = d0 < d1 ? 1 : 0;
            if (fromGate(n)) oneGate(n);
        } else if (mask == 1) {
            if (fromGate(0)) oneGate(0);
            else if (fromGate(1)) twoGates(0, 1);
        } else if (mask == 2) {
            if (fromGate(1)) oneGate(1);
            else if (fromGate(0)) twoGates(1, 0);
        }
    }
    W16(L.legCount, legs);
    W16(L.goalX, tx);
    W16(L.goalY, ty);
    const u8 cursor = R8(EngineFlags) & 8;
    if (i == 0) BCLR(EngineFlags, 3);
    setOriginActor(i);
    s16 vx, vy;
    const u16 facing = aim(tx, ty, speed, vx, vy);
    W16(L.stepX, u16(vx));
    W16(L.stepY, u16(vy));
    if (anim == 0) W16(kAnim + i * 2, R16(L.turnTable + (R16(L.facing) << 3) + facing * 2));
    else W16(kAnim + i * 2, u16(anim + facing));
    W16(L.facing, facing);
    W16(kTimer + i * 2, 0xFFFF);
    BSET(kRequest, i);
    spin([i] { return !BTST(kRequest, i) && BTST(kShown, i); });
    BSET(kWalking, i);
    if (i == 0) {
        if (RS16(vm::pc + 2) >= 0) {
            spin([] { return !BTST(kWalking, 0); });
            W16(kAnim, u16(facing + 4));
            W16(kTimer, 0xFFFF);
            BSET(kRequest, 0);
            spin([] { return !BTST(kRequest, 0); });
        }
        W8(EngineFlags, u8((R8(EngineFlags) & 0xF7) | cursor));
    }
    return facing;
}

}  // namespace

void update() {
    if (BTST(0xFF09E9, 3) && BTST(kRequest, 0)) {
        W16(kTimer + 4, R16(kTimer));
        W16(kAnim + 4, R16(0xFC760 + R16(kAnim) * 2));
        BSET(kRequest, 2);
    }
    for (int i = 5; i >= 0; i--) stepAnimation(i);
    for (int i = 5; i >= 0; i--) scalePending(i);

    // A lead and an attached partner are released together so they never
    // show frames from different poses.
    const int partner = R16(0xFF08B0) & 7;
    if (BTST(0xFF09E8, 3) && BTST(kPairReady, 0) && BTST(kPairReady, partner)) {
        W8(kPairReady, 0);
        const u32 header = R32(kBase) + s16(R16(kFrame));
        const s16 dx = s16((s32(RS16(header + 0x0E)) * s16(0xFF - R16(kScaleX))) >> 8);
        const s16 dy = s16((s32(RS16(header + 0x10)) * s16(0xFF - R16(kScaleY))) >> 8);
        const int p = RS16(0xFF08B0);
        W16(kPosX + p * 4, u16(R16(kPosX) - dx));
        W16(kPosY + p * 4, u16(R16(kPosY) - dy));
        BCLR(kScalePending, 0);
        BCLR(kScalePending, partner);
        BSET(kUpload, 0);
        BSET(kUpload, partner);
        while (BTST(kUpload, 0) || BTST(kUpload, partner)) ll::idleFrame();
    }
}

void objectTicks() {
    const u32 pc = vm::pc, end = vm::end;
    const u16 verb = vm::verb;
    const u32 loop = R32(0xFF0878);
    const u8 flags = R8(0xFF0AC9);
    const u32 hdr = R32(RoomHeaderPtr);
    u32 entry = R32(hdr + 0x28);
    const u32 scripts = R32(hdr + 0x2C);
    u32 o = 0xFF1200;
    for (int n = RS16(0xFF06F4); n > 0; n--, entry += 8, o += 0x1A) {
        if (!BTST(o + 0x18, 2)) continue;
        const u16 where = R16(o + 6);
        if (where != 1 && where != R16(0xFF06AC)) continue;
        W8(0xFF0AC9, u8((R8(0xFF0AC9) & 0x80) | 0x10));
        W32(0xFF0878, vm::ScanLoop);
        const u32 block = R32(entry) + scripts;
        vm::pc = block + 6;
        vm::end = vm::pc + RS16(block);
        vm::runScan();
        if (!BTST(0xFF0AC9, 0)) continue;
        W8(0xFF0AC9, R8(0xFF0AC9) & 0x80);
        BCLR(o + 0x18, 2);
        vm::end = vm::pc + RS16(vm::pc + 2);
        vm::pc += 4;
        W32(0xFF0878, vm::ExecLoop);
        vm::runExec();
    }
    W8(0xFF0AC9, flags);
    W32(0xFF0878, loop);
    vm::pc = pc;
    vm::end = end;
    vm::verb = verb;
}

void initLeads() {
    W32(kBase, 0x32700);
    W32(kBase + 4, 0x642D0);
    W16(kFrame, 0);
    W16(kFrame + 2, 0);
    W8(kActive, 3); W8(kRequest, 3); W8(kScalable, 3);
    W8(0xFF0AC8, 0); W8(kUpload, 0); W8(kWalking, 0); W8(kScalePending, 0);
    W8(0xFF0ABB, 0); W8(0xFF0ABF, 0); W8(0xFF0AC2, 0);
    W16(kAnim, 4); W16(kAnim + 2, 4);
    for (u32 a = 0xFF048C; a <= 0xFF0492; a += 2) W16(a, 0);
    for (u32 a = 0xFF05B8; a <= 0xFF05C2; a += 2) W16(a, 0);
    for (int i = 0; i < 6; i++) W16(kTimer + i * 2, 0xFFFF);
    for (u32 a = 0xFF0558; a <= 0xFF0584; a += 4) W8(a, 0xFF);
    W16(0xFF05C4, 0); W16(0xFF05D0, 0); W16(0xFF05C6, 0); W16(0xFF05D2, 0);
    update();
}

void walkLeadTo(u16 x, u16 y, u16 mode, u16 anim) {
    bool started;
    startWalk(kLead, x, y, mode, anim, started);
}

u16 companionWalk(u16 x, u16 y, u16 mode, u16 anim, bool& started) {
    return startWalk(kCompanion, x, y, mode, anim, started);
}

void walkCompanionTo(u16 x, u16 y, u16 mode, u16 anim) {
    bool started;
    const u16 facing = startWalk(kCompanion, x, y, mode, anim, started);
    if (!started || RS16(vm::pc + 2) < 0) return;
    spin([] { return !BTST(kWalking, 1); });
    W16(kAnim + 2, u16(facing + 4));
    W16(kTimer + 2, 0xFFFF);
    BSET(kRequest, 1);
    spin([] { return !BTST(kRequest, 1); });
}

// 0x5AD8: a straight walk with no route search and no early-out.
void companionWalkStart(u16 x, u16 y, u16 mode, u16 anim) {
    const Lead& L = kCompanion;
    W16(L.legCount, 0);
    W16(L.goalX, x);
    W16(L.goalY, y);
    setOriginActor(1);
    s16 vx, vy;
    const u16 facing = aim(x, y, mode, vx, vy);
    W16(L.stepX, u16(vx));
    W16(L.stepY, u16(vy));
    if (anim == 0) W16(kAnim + 2, R16(L.turnTable + (R16(L.facing) << 3) + facing * 2));
    else W16(kAnim + 2, u16(anim + facing));
    W16(L.facing, facing);
    W16(kTimer + 2, 0xFFFF);
    BSET(kRequest, 1);
    spin([] { return !BTST(kRequest, 1) && BTST(kShown, 1); });
    BSET(kWalking, 1);
}

void moveObjectActor(u16 who, u16 x, u16 y, s16 mode, u16 anim) {
    const u32 o = vm::objectAddr(who);
    W16(o + 0x12, x);
    W16(o + 0x14, y);
    const u8 slot = R8(o + 0x19);
    if (slot & 0x80) return;
    const int bit = slot + 2;
    if (mode < 0) {
        W16(0xFF04E4 + slot * 4, x);
        W16(0xFF04FC + slot * 4, y);
        if (s16(anim) < 0) return;
        W16(0xFF048C + slot * 2, anim);
        W16(0xFF061C + slot * 2, 0xFFFF);
        BSET(kRequest, bit & 7);
        spin([bit] { return BTST(kShown, bit & 7); });
        return;
    }
    const u32 unit = R32(0xFF051C + slot * 4);
    u32 speed = unit;
    for (int n = mode - 2; n >= 0; n--) speed += unit;
    W32(0xFF051C + slot * 4, speed);
    W32(0xFF0530 + slot * 4, speed >> 1);
    W16(0xFF05E0 + slot * 2, x);
    W16(0xFF05EC + slot * 2, y);
    if (s16(anim) >= 0) {
        W16(0xFF048C + slot * 2, anim);
        W16(0xFF061C + slot * 2, 0xFFFF);
        BSET(kRequest, bit & 7);
    }
    BSET(kWalking, bit & 7);
    if (RS16(vm::pc + 2) >= 0) spin([bit] { return !BTST(kWalking, bit & 7); });
}

namespace {

// Can the lead get to (x, y) in a straight line or through one gateway?
bool reachableFromLead(u16 x, u16 y) {
    setOriginActor(0);
    if (!lineBlocked(x, y)) return true;
    for (int n = 0; n < 2; n++) {
        u16 gx, gy;
        gateway(n, gx, gy);
        setOrigin(gx, gy);
        if (lineBlocked(x, y)) continue;
        setOriginActor(0);
        if (!lineBlocked(gx, gy)) return true;
    }
    return false;
}

bool pixelFree(u16 x, u16 y) {
    W16(0xFF064A, x);
    W16(0xFF064E, y);
    return !room::blockedProbe();
}

// The nearest free, reachable point inside an object's hotspot rectangle.
// Walk-on scripts fire when the lead's feet are inside that rectangle, so a
// click on such a hotspot should end up there.
bool pointInsideHotspot(int object, u16& outX, u16& outY) {
    if (object < 3) return false;
    const u32 o = vm::objectAddr(u16(object));
    if (BTST(o + 0x18, 1) || R16(o + 2) == 0) return false;
    const u32 hdr = R32(RoomHeaderPtr);
    const u32 r = R32(R32(hdr + 0x24) + s16(u16((R16(o + 2) - 1) << 2))) + R32(hdr + 0x1C);
    const int x0 = RS16(r) * 8, y0 = RS16(r + 2) * 8, x1 = x0 + RS16(r + 4) * 8, y1 = y0 + RS16(r + 6) * 8;
    const int lx = RS16(kPosX), ly = RS16(kPosY);
    long best = -1;
    for (int y = y1 - 1; y >= y0; y -= 2)
        for (int x = x0; x < x1; x += 2) {
            if (x < 0 || y < 0) continue;
            const long d = long(x - lx) * (x - lx) + long(y - ly) * (y - ly);
            if (best >= 0 && d >= best) continue;
            if (!pixelFree(u16(x), u16(y)) || !reachableFromLead(u16(x), u16(y))) continue;
            best = d;
            outX = u16(x);
            outY = u16(y);
        }
    return best >= 0;
}

}  // namespace

void clickWalk(int x, int y, int object) {
    {
        u16 hx, hy;
        if (pointInsideHotspot(object, hx, hy)) { x = hx; y = hy; }
    }
    const int maxX = (R16(0xFF06C6) << 3) - 1, maxY = (R16(0xFF06C8) << 3) - 1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > maxX) x = maxX;
    if (y > maxY) y = maxY;
    u16 tx = u16(x), ty = u16(y);
    // Reachable directly or from one of the two gateways? Otherwise stop at
    // the last free point on the straight line.
    setOriginActor(0);
    bool reachable = !lineBlocked(tx, ty);
    const u16 stopX = lastClearX, stopY = lastClearY;
    for (int n = 0; n < 2 && !reachable; n++) {
        u16 gx, gy;
        gateway(n, gx, gy);
        setOrigin(gx, gy);
        if (lineBlocked(tx, ty)) continue;
        setOriginActor(0);
        reachable = !lineBlocked(gx, gy);
    }
    if (!reachable) { tx = stopX; ty = stopY; }
    if (tx == R16(kPosX) && ty == R16(kPosY)) return;
    // Scripted walks expect the "busy" flag, and read their wait option from
    // the current instruction; point that at a zero word.
    const u32 savedPc = vm::pc;
    W16(0xFFFFF0, 0); W16(0xFFFFF2, 0);
    vm::pc = 0xFFFFF0;
    BSET(EngineFlags, 7);
    walkLeadTo(tx, ty, 1, 0);
    BCLR(EngineFlags, 7);
    vm::pc = savedPc;
}

u16 standOffset(u32 base, u16 index, u16* yOffset) {
    u32 p = base + s16(R16(base + s16(u16(index * 2 + R16(base + 2)))));
    while (R16(p) != 2) p += 4;
    const u32 frame = base + s16(R16(p + 2));
    if (yOffset) *yOffset = R16(frame + 0x10);
    return R16(frame + 0x0E);
}

}  // namespace actor
