#include "savestate.h"
#include "../core/machine.h"
#include "addr.h"
#include <cstring>

namespace savestate {

namespace {

// File layout: 8 bytes magic, 32 bytes name, episode, room, two spare bytes,
// 4 bytes sound state length (little-endian), then RAM, video RAM, colour
// RAM, scroll RAM, video registers and the sound state.
const char kMagic[8] = {'S', 'C', 'B', 'Y', 'S', 'A', 'V', '1'};
constexpr size_t kHeader = 8 + kNameLength + 4 + 4;
constexpr size_t kFixed = kHeader + 0x10000 + 0x10000 + 64 * 2 + 40 * 2 + 32;

}  // namespace

std::vector<u8> capture() {
    std::vector<u8> audio;
    if (M.audioSave) M.audioSave(audio);
    std::vector<u8> d;
    d.reserve(kFixed + audio.size());
    d.insert(d.end(), kMagic, kMagic + 8);
    d.insert(d.end(), kNameLength, 0);
    d.push_back(u8(R16(EpisodeIndex)));
    d.push_back(u8(R16(0xFF06AC)));
    d.push_back(0); d.push_back(0);
    for (int i = 0; i < 4; i++) d.push_back(u8(audio.size() >> (8 * i)));
    d.insert(d.end(), M.ram, M.ram + 0x10000);
    d.insert(d.end(), M.vdp.vram.begin(), M.vdp.vram.end());
    for (u16 c : M.vdp.cram) { d.push_back(u8(c >> 8)); d.push_back(u8(c)); }
    for (u16 c : M.vdp.vsram) { d.push_back(u8(c >> 8)); d.push_back(u8(c)); }
    d.insert(d.end(), M.vdp.reg.begin(), M.vdp.reg.end());
    d.insert(d.end(), audio.begin(), audio.end());
    return d;
}

bool read(const std::vector<u8>& d, Info& out) {
    if (d.size() < kFixed || std::memcmp(d.data(), kMagic, 8) != 0) return false;
    const u8* h = d.data() + 8;
    size_t n = 0;
    while (n < kNameLength && h[n]) n++;
    out.name.assign(reinterpret_cast<const char*>(h), n);
    out.episode = h[kNameLength];
    out.room = h[kNameLength + 1];
    const u8* a = h + kNameLength + 4;
    const size_t audio = size_t(a[0]) | size_t(a[1]) << 8 | size_t(a[2]) << 16 | size_t(a[3]) << 24;
    return out.episode <= 1 && d.size() == kFixed + audio;
}

void setName(std::vector<u8>& d, const std::string& name) {
    if (d.size() < kHeader) return;
    std::memset(d.data() + 8, 0, kNameLength);
    std::memcpy(d.data() + 8, name.data(), name.size() < kNameLength - 1 ? name.size() : kNameLength - 1);
}

bool apply(const std::vector<u8>& d) {
    Info info;
    if (!read(d, info)) return false;
    const u8* p = d.data() + kHeader;
    std::memcpy(M.ram, p, 0x10000); p += 0x10000;
    std::memcpy(M.vdp.vram.data(), p, 0x10000); p += 0x10000;
    for (u16& c : M.vdp.cram) { c = u16(p[0] << 8 | p[1]); p += 2; }
    for (u16& c : M.vdp.vsram) { c = u16(p[0] << 8 | p[1]); p += 2; }
    std::memcpy(M.vdp.reg.data(), p, 32); p += 32;
    if (M.audioLoad) M.audioLoad(p, size_t(d.data() + d.size() - p));
    // Nothing the pointer was doing before the load applies any more.
    M.walkRequest = false;
    M.mouseMoved = true;
    return true;
}

void atIdle() {
    if (!M.loadData.empty()) {
        apply(M.loadData);
        M.loadData.clear();
    }
    if (M.snapshotRequested) {
        M.snapshot = capture();
        M.snapshotRequested = false;
        M.snapshotSerial++;
    }
}

}  // namespace savestate
