#include "autoplay.h"
#include "../core/machine.h"
#include "addr.h"
#include "room.h"
#include <cstdio>
#include <execinfo.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <algorithm>
#include <array>
#include <vector>

namespace autoplay {

bool active = false, done = false;
std::string result;
u32 idleCalls = 0;
bool abortRequested = false;

namespace {

struct Snapshot {
    u8 ram[0x10000];
    std::array<u8, 0x10000> vram;
    std::array<u16, 64> cram;
    std::array<u16, 40> vsram;
    std::array<u8, 32> reg;
};

struct Action { u16 verb, object, second; };
const u16 kWait = 15;   // pseudo verb: do nothing for a few seconds

// One room visited while looking for the next step. `nav` is how to get
// there from the state the current search level started in.
struct RoomNode {
    std::unique_ptr<Snapshot> snap;
    std::vector<Action> actions, nav;
    size_t next = 0;
};

std::vector<RoomNode> level;        // breadth-first over rooms
size_t levelAt = 0;
std::set<u16> roomsSeen;
std::set<unsigned long long> nodesSeen;            // room plus story state plus available actions
const size_t kMaxNodes = 20000;     // per level
const int kCounterValues = 24;      // a spare word with this many values is a counter, not story state
std::set<u32> facts;                // every story fact seen so far
std::map<u32, int> spareValues;     // distinct values seen per spare object word
std::vector<Action> replay;         // actions to run before searching
size_t replayAt = 0;
bool searchAfterReplay = true;
std::vector<Action> path;           // committed actions from the start
enum class Phase { Start, AfterAction, Expand } phase = Phase::Start;
int settle = 0;
Action pending{0, 0, 0};
long tried = 0, steps = 0;
u16 choicePlan = 0;   // base-6 digits, lowest first: which open line to say at each prompt
std::vector<std::pair<u16, Action>> hung;   // room and action that never returned

void take(Snapshot& s) {
    std::memcpy(s.ram, M.ram, sizeof s.ram);
    s.vram = M.vdp.vram; s.cram = M.vdp.cram; s.vsram = M.vdp.vsram; s.reg = M.vdp.reg;
}
void restore(const Snapshot& s) {
    std::memcpy(M.ram, s.ram, sizeof s.ram);
    M.vdp.vram = s.vram; M.vdp.cram = s.cram; M.vdp.vsram = s.vsram; M.vdp.reg = s.reg;
}

u32 objectAddr(u16 number) { return 0xFF1200 + u32(number - 3) * 0x1A; }
u16 objectCount() { return u16(R16(0xFF06F4) + 1); }

// Story facts of the current state: each flag bit's value, and each object's
// room, picture, hotspot, icon and suggested verb. Returns how many are new,
// and records them.
int learnFacts() {
    int fresh = 0;
    auto add = [&](u32 f) { if (facts.insert(f).second) fresh++; };
    for (u32 i = 0; i < 0x1D; i++) {
        const u8 v = R8(0xFF2A00 + i);
        for (u32 bit = 0; bit < 8; bit++) {
            if (i == 0 && bit >= 1 && bit <= 4) continue;   // scratch bits
            add(0x10000000u | i << 8 | bit << 1 | ((v >> bit) & 1));
        }
    }
    for (u32 n = 0; n < objectCount(); n++) {
        const u32 o = 0xFF1200 + n * 0x1A;
        add(0x20000000u | n << 16 | R16(o + 6));
        add(0x30000000u | n << 16 | (R16(o) & 0x3FFF));
        add(0x40000000u | n << 16 | R16(o + 2));
        add(0x50000000u | n << 16 | R16(o + 4));
        add(0x60000000u | n << 16 | R8(o + 0x16));
        // Spare words that scripts use as counters and bit sets (the six
        // hallway doors collect bits in one). Characters keep positions there.
        // A word that keeps changing is a counter, so only its first few
        // values count as news.
        if (!BTST(o + 0x18, 1))
            for (u32 w = 8; w <= 0x10; w += 2) {
                const u32 key = (0x70000000u + (w << 27)) ^ (n << 16 | R16(o + w));
                if (facts.count(key)) continue;
                int& seen = spareValues[n << 8 | w];
                if (seen < kCounterValues) { seen++; add(key); }
            }
    }
    return fresh;
}

bool inReach(u16 number, bool mustBeHere = false) {
    const u32 o = objectAddr(number);
    const u16 where = R16(o + 6);
    if (where == 1) return !mustBeHere;
    if (where != R16(0xFF06AC)) return false;
    if (BTST(o + 0x18, 1)) return !(R8(o + 0x19) & 0x80);
    return R16(o + 2) != 0;
}

// Instruction sizes, for walking a script to collect its verb headers.
int sizeOf(u32 pc) {
    static const int kSize[0x22] = {2, 8, 14, 16, 16, 6, 6, 4, 12, 6, 6, 6, 4, 4, 10, 12, 8, 8, 6, 4, 6, 4, 4, 4, 8, 4, 10, 12, 4, 4, 6, 4, 4, 0};
    const u16 op = R16(pc);
    if (op == 0x21) return 8 + RS16(pc + 2);
    return op < 0x21 ? kSize[op] : 0;
}

// Which parts of the current room can the lead walk to? A flood fill over
// the collision map, pixel by pixel, from where he stands.
struct Walkable {
    int w = 0, h = 0;
    std::vector<u8> cell;
    bool at(int gx, int gy) const { return gx >= 0 && gy >= 0 && gx < w && gy < h && cell[size_t(gy) * w + gx]; }
};

Walkable floodFromLead() {
    Walkable f;
    f.w = R16(0xFF06C6) * 8;
    f.h = R16(0xFF06C8) * 8;
    f.cell.assign(size_t(f.w) * f.h, 0);
    const u16 sx = R16(0xFF064A), sy = R16(0xFF064E);
    auto freeAt = [](int gx, int gy) {
        W16(0xFF064A, u16(gx));
        W16(0xFF064E, u16(gy));
        return !room::blockedProbe();
    };
    std::vector<std::pair<int, int>> todo;
    const int x0 = RS16(0xFF04DC), y0 = RS16(0xFF04F4);
    for (int dy = -6; dy <= 6; dy++)   // scripted moves can leave him a few pixels inside a wall
        for (int dx = -6; dx <= 6; dx++) {
            const int gx = x0 + dx, gy = y0 + dy;
            if (gx < 0 || gy < 0 || gx >= f.w || gy >= f.h || f.cell[size_t(gy) * f.w + gx]) continue;
            if (!freeAt(gx, gy)) continue;
            f.cell[size_t(gy) * f.w + gx] = 1;
            todo.push_back({gx, gy});
        }
    while (!todo.empty()) {
        const auto [cx, cy] = todo.back();
        todo.pop_back();
        static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& s : d) {
            const int gx = cx + s[0], gy = cy + s[1];
            if (gx < 0 || gy < 0 || gx >= f.w || gy >= f.h || f.cell[size_t(gy) * f.w + gx]) continue;
            if (!freeAt(gx, gy)) continue;
            f.cell[size_t(gy) * f.w + gx] = 1;
            todo.push_back({gx, gy});
        }
    }
    W16(0xFF064A, sx);
    W16(0xFF064E, sy);
    return f;
}

// Can he walk into this object's hotspot rectangle?
bool canWalkOnto(const Walkable& f, u16 number) {
    const u32 o = objectAddr(number);
    if (R16(o + 2) == 0) return false;
    const u32 hdr = R32(RoomHeaderPtr);
    const u32 r = R32(R32(hdr + 0x24) + u32(R16(o + 2) - 1) * 4) + R32(hdr + 0x1C);
    const int x0 = RS16(r) * 8, y0 = RS16(r + 2) * 8, x1 = x0 + RS16(r + 4) * 8, y1 = y0 + RS16(r + 6) * 8;
    for (int gy = y0; gy < y1; gy++)
        for (int gx = x0; gx < x1; gx++)
            if (f.at(gx, gy)) return true;
    return false;
}

std::vector<Action> enumerate() {
    // Order matters only for speed: verbs that usually advance things first,
    // exits after, destructive or reversing verbs last.
    static const int kRank[16] = {9, 9, 0, 3, 8, 2, 2, 4, 1, 7, 5, 6, 9, 9, 9, 9};
    std::vector<std::pair<int, Action>> found;
    const u32 hdr = R32(RoomHeaderPtr);
    const Walkable floor = floodFromLead();
    for (u16 n = 0; n < objectCount(); n++) {
        const u16 number = u16(n + 3);
        if (!inReach(number)) continue;
        const u32 block = R32(hdr + 0x2C) + R32(R32(hdr + 0x28) + u32(n) * 8);
        std::set<u32> had;
        for (u32 pc = block + 6, end = block + 6 + R16(block); pc < end;) {
            const int sz = sizeOf(pc);
            if (sz <= 0) break;
            if (R16(pc) == 1) {
                const u16 verb = R16(pc + 2), second = R16(pc + 4);
                const bool here = R16(objectAddr(number) + 6) != 1;
                bool ok = verb >= 1 && verb <= 11;
                if (verb == 11 && (!here || !canWalkOnto(floor, number))) ok = false;
                if (verb == 11 && BTST(0xFF09E8, 2)) ok = false;   // he cannot walk while bouncing on the spring
                if (second >= 3 && !inReach(second)) ok = false;
                if (ok && had.insert(u32(verb) << 16 | second).second) {
                    found.push_back({kRank[verb & 15], Action{verb, number, second}});
                    if (verb == 8 && second == 0)   // other ways through the conversation: first two picks
                        for (u16 plan = 1; plan < 36; plan++) found.push_back({kRank[8] + 1, Action{verb, number, plan}});
                    if (verb == 11 && second == 0)   // a walk-on may lead to a fork (the fun house entrance)
                        for (u16 plan = 1; plan < 3; plan++) found.push_back({kRank[11] + 1, Action{verb, number, plan}});
                }
            }
            pc += u32(sz);
        }
    }
    found.push_back({10, Action{kWait, 0, 0}});   // some scenes move on by themselves (the bungee jump)
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Action> out;
    for (const auto& f : found) out.push_back(f.second);
    return out;
}

// Identity of a search node. Two visits to a room are the same node only if
// the story state and the actions on offer are the same, so that undoing
// something (a switch turned back off) can be found and redone.
unsigned long long nodeKey(const std::vector<Action>& actions) {
    unsigned long long h = 1469598103934665603ull;
    auto mix = [&](u32 v) { for (int i = 0; i < 4; i++) { h ^= (v >> (i * 8)) & 0xFF; h *= 1099511628211ull; } };
    mix(R16(0xFF06AC));
    for (u32 i = 0; i < 0x1D; i++) mix(i == 0 ? R8(0xFF2A00) & 0xE1 : R8(0xFF2A00 + i));
    for (u32 n = 0; n < objectCount(); n++) {
        const u32 o = 0xFF1200 + n * 0x1A;
        if (n == 0) continue;   // object 3 is the scripts' scratch space
        mix(R16(o) & 0x3FFF); mix(R16(o + 2)); mix(R16(o + 4)); mix(R16(o + 6)); mix(R8(o + 0x16));
        if (!BTST(o + 0x18, 1))
            for (u32 w = 8; w <= 0x10; w += 2) {
                const auto it = spareValues.find(n << 8 | w);
                if (it == spareValues.end() || it->second < kCounterValues) mix(R16(o + w));   // not a running counter
            }
    }
    for (const Action& a : actions) { mix(a.verb); mix(a.object); mix(a.second); }
    return h;
}

void issue(const Action& a) {
    if (a.verb == kWait) return;
    const bool planned = a.verb == 8 || a.verb == 11;   // `second` is a plan of choices for these
    choicePlan = planned ? a.second : 0;
    W16(0xFF06AE, a.object);
    W16(0xFF06B4, 0);
    W16(0xFF06BE, a.verb);
    W16(0xFF06C0, a.verb == 11 ? 0 : a.verb);
    const u16 second = planned ? 0 : a.second;
    W16(0xFF06B6, second);
    if (second) BSET(0xFF09DE, 0); else BCLR(0xFF09DE, 0);
    BCLR(0xFF0AC9, 7);
    BCLR(EngineFlags, 3);
    BSET(EngineFlags, 7);
}

std::string nameOf(u16 number) {
    const u32 hdr = R32(RoomHeaderPtr);
    u32 a = R32(hdr + 0x20) + R32(R32(hdr + 0x28) + u32(number - 3) * 8 + 4);
    std::string s;
    while (u8 c = R8(a++)) s.push_back(char(c));
    return s;
}

// For a written solution: the room, and where the object is among the
// things in that room with the same name, counted from the left.
struct Note { u16 room; int rank, count; };
std::vector<Note> notes;
Note describe(const Action& a) {
    Note n{R16(0xFF06AC), 0, 0};
    if (a.verb == kWait || R16(objectAddr(a.object) + 6) == 1) return n;
    const u32 hdr = R32(RoomHeaderPtr);
    auto leftEdge = [&](u16 number) {
        const u32 o = objectAddr(number);
        if (BTST(o + 0x18, 1)) return int(RS16(o + 0x12));
        return int(RS16(R32(R32(hdr + 0x24) + u32(R16(o + 2) - 1) * 4) + R32(hdr + 0x1C))) * 8;
    };
    const std::string name = nameOf(a.object);
    const int x = leftEdge(a.object);
    for (u16 k = 0; k < objectCount(); k++) {
        const u16 number = u16(k + 3);
        if (!inReach(number, true) || nameOf(number) != name) continue;
        n.count++;
        const int ox = leftEdge(number);
        if (ox < x || (ox == x && number <= a.object)) n.rank++;
    }
    return n;
}

void writePath(const char* outcome) {
    char file[64];
    std::snprintf(file, sizeof file, "work/out/%s_ep%d.txt", searchAfterReplay ? "solve" : "replay", R16(EpisodeIndex));
    std::ofstream f(file);
    f << "# " << outcome << "; actions tried " << tried << "\n# verb object second\n";
    std::vector<Action> all = path;
    if (levelAt < level.size()) all.insert(all.end(), level[levelAt].nav.begin(), level[levelAt].nav.end());
    if (pending.verb) all.push_back(pending);
    for (const Action& a : all) f << a.verb << ' ' << a.object << ' ' << a.second << "\n";
    for (const auto& h : hung) f << "# never returns (softlock): room " << h.first << " action " << h.second.verb << ' ' << h.second.object << ' ' << h.second.second << "\n";
    for (size_t i = 0; i < notes.size(); i++) f << "#@ " << i << ' ' << notes[i].room << ' ' << notes[i].rank << ' ' << notes[i].count << "\n";
    f << "# rooms reachable at the end:";
    for (u16 r : roomsSeen) f << ' ' << r;
    f << "\n# carried:";
    for (u32 p = 0xFF0A1E; RS16(p) >= 0; p += 2) f << ' ' << (R16(p) + 3) << '=' << nameOf(u16(R16(p) + 3)) << ';';
    f << "\n# story flags:";
    for (int i = 0; i < 0x1D; i++) { char h[4]; std::snprintf(h, sizeof h, " %02X", R8(0xFF2A00 + i)); f << h; }
    f << "\n";
    f << "# objects in reachable rooms (number name room hotspot state):\n";
    for (u16 n = 0; n < objectCount(); n++) {
        const u32 o = 0xFF1200 + n * 0x1A;
        if (R16(o + 6) > 1 && roomsSeen.count(R16(o + 6)))
            f << "#   " << (n + 3) << ' ' << nameOf(u16(n + 3)) << " room " << R16(o + 6) << " hot " << R16(o + 2) << " state " << (R16(o) & 0x3FFF) << "\n";
    }
    f << "# objects elsewhere (number name room):\n";
    for (u16 n = 0; n < objectCount(); n++) {
        const u32 o = 0xFF1200 + n * 0x1A;
        if (R16(o + 6) > 1 && !roomsSeen.count(R16(o + 6))) f << "#.  " << (n + 3) << ' ' << nameOf(u16(n + 3)) << " room " << R16(o + 6) << "\n";
    }
    f << "# two-object handlers whose second object is not in reach (verb object second):\n";
    {
        const u32 hdr = R32(RoomHeaderPtr);
        for (u16 n = 0; n < objectCount(); n++) {
            const u32 o = 0xFF1200 + n * 0x1A;
            if (!(R16(o + 6) == 1 || roomsSeen.count(R16(o + 6)))) continue;
            const u32 block = R32(hdr + 0x2C) + R32(R32(hdr + 0x28) + u32(n) * 8);
            for (u32 pc = block + 6, end = block + 6 + R16(block); pc < end;) {
                const int sz = sizeOf(pc);
                if (sz <= 0) break;
                if (R16(pc) == 1 && R16(pc + 4) >= 3) {
                    const u16 second = R16(pc + 4);
                    const u16 where = R16(objectAddr(second) + 6);
                    if (!(where == 1 || roomsSeen.count(where)))
                        f << "#   " << R16(pc + 2) << ' ' << (n + 3) << '=' << nameOf(u16(n + 3)) << " needs " << second << '=' << nameOf(second) << " (now in room " << where << ")\n";
                }
                pc += u32(sz);
            }
        }
    }
    char buf[220];
    std::snprintf(buf, sizeof buf, "%s after %zu actions (%ld story steps, %ld actions tried); path in %s", outcome, all.size(), steps, tried, file);
    result = buf;
}

void startLevel() {
    level.clear();
    levelAt = 0;
    roomsSeen.clear();
    nodesSeen.clear();
    roomsSeen.insert(R16(0xFF06AC));
    RoomNode n;
    n.snap = std::make_unique<Snapshot>();
    take(*n.snap);
    n.actions = enumerate();
    nodesSeen.insert(nodeKey(n.actions));
    level.push_back(std::move(n));
}

}  // namespace

void loadReplay(const std::string& file, bool thenSearch) {
    std::ifstream f(file);
    std::string line;
    while (std::getline(f, line)) {
        unsigned v, o, s2;
        if (line.empty() || line[0] == '#') continue;
        if (std::sscanf(line.c_str(), "%u %u %u", &v, &o, &s2) == 3) replay.push_back(Action{u16(v), u16(o), u16(s2)});
    }
    searchAfterReplay = thenSearch;
}

int chooseLine(int count) {
    const int pick = choicePlan % 6;
    choicePlan /= 6;
    return pick < count ? pick : count - 1;
}

void checkAbort() {
    if (abortRequested) {
        abortRequested = false;
        if (getenv("SCOOBY_ABORTTRACE")) { void* bt[24]; backtrace_symbols_fd(bt, backtrace(bt, 24), 2); }
        throw Abort{};
    }
}

// The action being tried never came back to the main loop (the cartridge
// has a few of these, such as walking somewhere while bouncing on the bed
// spring). Note it, go back to the state it was tried from and carry on.
void aborted() {
    if (phase != Phase::AfterAction || levelAt >= level.size() || !level[levelAt].snap) { stalled(); return; }
    restore(*level[levelAt].snap);
    const std::pair<u16, Action> h{R16(0xFF06AC), pending};
    bool known = false;
    for (const auto& k : hung) known |= k.first == h.first && k.second.verb == pending.verb && k.second.object == pending.object && k.second.second == pending.second;
    if (!known) {
        hung.push_back(h);
        char file[80];
        std::snprintf(file, sizeof file, "work/out/hang_r%d_%d_%d_%d.txt", h.first, pending.verb, pending.object, pending.second);
        std::ofstream f(file);
        for (const Action& a : path) f << a.verb << ' ' << a.object << ' ' << a.second << "\n";
        for (const Action& a : level[levelAt].nav) f << a.verb << ' ' << a.object << ' ' << a.second << "\n";
        f << pending.verb << ' ' << pending.object << ' ' << pending.second << "\n";
        std::fprintf(stderr, "solver: action %d %d %d in room %d never returns; skipped (path in %s)\n", pending.verb, pending.object, pending.second, h.first, file);
    }
    pending = Action{0, 0, 0};
    phase = Phase::Expand;
    settle = 0;
}

void stalled() {
    std::fprintf(stderr, "solver: stall state: engine %02X slide %02X 09DE %02X 09E8 %02X 09E9 %02X 09EB %02X flag0 %02X active %02X request %02X shown %02X walking %02X finished %02X anims %04X %04X %04X timers %04X %04X %04X\n",
                 R8(EngineFlags), R8(0xFF0ACB), R8(0xFF09DE), R8(0xFF09E8), R8(0xFF09E9), R8(0xFF09EB), R8(0xFF2A00), R8(0xFF0AB4), R8(0xFF0AB5), R8(0xFF0ABD), R8(0xFF0AB7), R8(0xFF0ABC), R16(0xFF0488), R16(0xFF048A), R16(0xFF048C), R16(0xFF0618), R16(0xFF061A), R16(0xFF061C));
    writePath("game stalled (last action listed is the one that never returned)");
    done = true;
}

void programRestarted() {
    if (!active || done) return;
    writePath("episode reached its ending");
    done = true;
}

void idle() {
    idleCalls++;
    if (!active || done) return;
    if (settle > 0) { settle--; return; }
    if (BTST(0xFF09DE, 5)) return;   // let the companion finish settling first
    if (replayAt < replay.size()) {
        const Action a = replay[replayAt++];
        // Only what a player could do here: the action must be on offer.
        bool offered = false;
        for (const Action& e : enumerate()) offered |= e.verb == a.verb && e.object == a.object && e.second == a.second;
        if (!offered) {
            pending = a;
            writePath("replay failed: the last action listed is not possible there");
            done = true;
            return;
        }
        path.push_back(a);
        notes.push_back(describe(a));
        learnFacts();
        roomsSeen.insert(R16(0xFF06AC));
        issue(a);
        settle = a.verb == kWait ? 240 : 30;
        return;
    }
    if (!replay.empty() && !searchAfterReplay) {
        roomsSeen.insert(R16(0xFF06AC));
        writePath("replay finished");
        done = true;
        return;
    }
    if (phase == Phase::Start) {
        BCLR(0xFF0ACA, 0);   // let skippable sequences play
        learnFacts();
        startLevel();
        phase = Phase::Expand;
    } else if (phase == Phase::AfterAction) {
        const int fresh = learnFacts();
        if (fresh > 0) {   // progress: commit and search on from here
            RoomNode& from = level[levelAt];
            path.insert(path.end(), from.nav.begin(), from.nav.end());
            path.push_back(pending);
            steps++;
            if (steps % 10 == 0) std::fprintf(stderr, "solver: %ld story steps, %zu actions on the path, room %d, %ld tried\n", steps, path.size(), R16(0xFF06AC), tried);
            pending = Action{0, 0, 0};
            startLevel();
        } else if (level.size() < kMaxNodes) {
            std::vector<Action> acts = enumerate();
            if (nodesSeen.insert(nodeKey(acts)).second) {   // a room or state not yet tried at this level
                roomsSeen.insert(R16(0xFF06AC));
                RoomNode n;
                n.snap = std::make_unique<Snapshot>();
                take(*n.snap);
                n.actions = std::move(acts);
                n.nav = level[levelAt].nav;
                n.nav.push_back(pending);
                level.push_back(std::move(n));
            }
        }
        pending = Action{0, 0, 0};
        phase = Phase::Expand;
    }
    while (levelAt < level.size()) {
        RoomNode& node = level[levelAt];
        if (node.next < node.actions.size()) {
            pending = node.actions[node.next++];
            restore(*node.snap);
            issue(pending);
            tried++;
            settle = pending.verb == kWait ? 240 : 30;
            phase = Phase::AfterAction;
            return;
        }
        node.snap.reset();   // done with this node
        levelAt++;
    }
    levelAt = level.size() ? level.size() - 1 : 0;
    char where[96];
    std::snprintf(where, sizeof where, "no action in %zu reachable rooms (%zu states) makes progress", roomsSeen.size(), level.size());
    pending = Action{0, 0, 0};
    writePath(where);
    done = true;
}

}  // namespace autoplay
