#include "machine.h"
#include <fstream>
#include <iterator>

Machine M;

std::string Machine::loadRom(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "cannot open " + path;
    rom.assign(std::istreambuf_iterator<char>(f), {});
    if (rom.size() != 0x200000) return "unexpected ROM size (expected 2 MB cartridge image)";
    if (std::string(rom.begin() + 0x100, rom.begin() + 0x104) != "SEGA") return "not a Mega Drive image";
    // The cartridge's own checksum: 16-bit sum of every word after the header.
    u16 sum = 0;
    for (size_t i = 0x200; i < rom.size(); i += 2) sum = u16(sum + (rom[i] << 8 | rom[i + 1]));
    const u16 want = u16(rom[0x18E] << 8 | rom[0x18F]);
    if (sum != want || want != 0x9EC6) return "checksum mismatch: this build targets the USA release (checksum 9EC6)";
    return "";
}

void Machine::start(void (*entry)()) {
    started_ = true;
    thread_ = std::thread([this, entry] {
        toGame_.acquire();
        try {
            if (!stop_) entry();
        } catch (const StopGame&) {
        }
        finished_ = true;
        toHost_.release();
    });
}

void Machine::runGameSlice() {
    if (!started_ || finished_) return;
    toGame_.release();
    toHost_.acquire();
}

void Machine::yieldFrame() {
    syncCounter++;
    toHost_.release();
    toGame_.acquire();
    if (stop_) throw StopGame{};
}

void Machine::shutdown() {
    if (!started_) return;
    stop_ = true;
    if (!finished_) {
        toGame_.release();
        toHost_.acquire();
    }
    if (thread_.joinable()) thread_.join();
}
