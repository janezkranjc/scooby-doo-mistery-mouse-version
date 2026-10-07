// The console's sound side: a Z80 running the cartridge's own sound driver,
// an FM chip and a PSG. The driver program and all music data are read from
// the ROM at run time. The game feeds it through a 64-byte command queue in
// Z80 RAM, exactly as the original 68000 code does.
#pragma once
#include "types.h"
#include <memory>
#include <mutex>
#include <vector>

class AudioMachine {
public:
    static constexpr int kSampleRate = 53267;   // FM chip clock / 144

    explicit AudioMachine(const std::vector<u8>& rom);
    ~AudioMachine();

    // Driver control, called from the game thread.
    void loadDriver(u32 start, u32 end);   // copy the Z80 program and reset
    void queueByte(u8 b);                  // append to the command queue
    void command(u8 code);                 // 0xFF marker plus a command byte

    // Renders interleaved stereo frames. Safe to call from an audio thread.
    void render(s16* out, int frames);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    std::mutex lock_;
};
