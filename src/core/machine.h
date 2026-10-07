// The host-side stand-in for the console: cartridge image, 64 KB of work RAM,
// the video chip model, pad state and the frame hand-off between the game
// thread and the presentation loop.
//
// Game state is kept in a RAM image with the original layout. The scripts in
// the cartridge address engine variables by absolute location, so keeping the
// layout is what lets the original data drive the reimplemented engine.
#pragma once
#include "types.h"
#include "vdp.h"
#include <atomic>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

// Pad bits as the game stores them (active low in RAM, active high here).
enum PadBit : u8 {
    PadUp = 0x01, PadDown = 0x02, PadLeft = 0x04, PadRight = 0x08,
    PadB = 0x10, PadC = 0x20, PadA = 0x40, PadStart = 0x80,
};

struct StopGame {};   // thrown inside the game thread to unwind it on shutdown

class Machine {
public:
    std::vector<u8> rom;
    u8 ram[0x10000] = {};
    Vdp vdp;

    u8 pad1 = 0, pad2 = 0;          // currently held buttons (active high)
    // Pointer device, in game pixels. `mouseMoved` stays set until the pointer
    // code has taken the new position.
    bool mouseEnabled = false, mouseMoved = false;
    bool mouseActive = false;       // the player is steering with the mouse
    bool skipRequested = false;     // a button went down this frame (for skippable screens)
    int mouseX = 0, mouseY = 0;
    bool mouseLeft = false, mouseRight = false, mouseMiddle = false;   // held buttons, for the menus
    int mouseWheel = 0;             // notches turned since last read, up positive
    // Set by the frame work when the pointer is clicked on open floor.
    bool walkRequest = false;
    int walkX = 0, walkY = 0;
    int walkObject = 0;             // script object under the pointer at the click, or 0
    int debugRoom = 0;              // testing aid: jump to this room once the episode has started
    std::vector<std::pair<u32, u8>> debugPokes;   // testing aid: bytes written before that jump
    u32 frameCounter = 0;           // host frames presented
    u32 syncCounter = 0;            // frame waits performed by the game thread

    // Loads and validates the cartridge image. Returns an error message or "".
    std::string loadRom(const std::string& path);

    // ---- memory, addressed like the 68000 bus ----
    u8 rd8(u32 a) const {
        a &= 0xFFFFFF;
        if (a < rom.size()) return rom[a];
        if (a >= 0xFF0000) return ram[a & 0xFFFF];
        return 0xFF;
    }
    u16 rd16(u32 a) const { return u16(rd8(a) << 8 | rd8(a + 1)); }
    u32 rd32(u32 a) const { return u32(rd16(a)) << 16 | rd16(a + 2); }
    void wr8(u32 a, u8 v) { a &= 0xFFFFFF; if (a >= 0xFF0000) ram[a & 0xFFFF] = v; }
    void wr16(u32 a, u16 v) { wr8(a, u8(v >> 8)); wr8(a + 1, u8(v)); }
    void wr32(u32 a, u32 v) { wr16(a, u16(v >> 16)); wr16(a + 2, u16(v)); }
    const u8* ptr(u32 a) const {
        a &= 0xFFFFFF;
        return a < rom.size() ? &rom[a] : &ram[a & 0xFFFF];
    }

    // ---- frame hand-off ----
    // Starts `entry` on the game thread. It runs until its first frame wait.
    void start(void (*entry)());
    // Presentation side: lets the game thread run until its next frame wait.
    void runGameSlice();
    // Game side: gives the frame back and blocks until the next one.
    void yieldFrame();
    void shutdown();
    bool gameFinished() const { return finished_; }

private:
    std::thread thread_;
    std::binary_semaphore toGame_{0}, toHost_{0};
    std::atomic<bool> stop_{false}, finished_{false}, started_{false};
};

extern Machine M;

// Terse accessors for the engine code.
inline u8 R8(u32 a) { return M.rd8(a); }
inline u16 R16(u32 a) { return M.rd16(a); }
inline u32 R32(u32 a) { return M.rd32(a); }
inline s16 RS16(u32 a) { return s16(M.rd16(a)); }
inline s8 RS8(u32 a) { return s8(M.rd8(a)); }
inline void W8(u32 a, u8 v) { M.wr8(a, v); }
inline void W16(u32 a, u16 v) { M.wr16(a, v); }
inline void W32(u32 a, u32 v) { M.wr32(a, v); }
inline bool BTST(u32 a, int bit) { return (M.rd8(a) >> bit) & 1; }
inline void BSET(u32 a, int bit) { M.wr8(a, u8(M.rd8(a) | (1 << bit))); }
inline void BCLR(u32 a, int bit) { M.wr8(a, u8(M.rd8(a) & ~(1 << bit))); }
inline void BCHG(u32 a, int bit) { M.wr8(a, u8(M.rd8(a) ^ (1 << bit))); }
