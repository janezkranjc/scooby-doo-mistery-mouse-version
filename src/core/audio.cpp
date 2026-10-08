#include "audio.h"
#include <cstring>
#include <type_traits>

extern "C" {
#include "z80.h"
}
#include "ymfm_opn.h"

namespace {

// Texas Instruments style PSG: three square channels and a noise channel.
class Psg {
public:
    void write(u8 v) {
        if (v & 0x80) {
            latch_ = (v >> 4) & 7;
            const int ch = latch_ >> 1;
            if (latch_ & 1) vol_[ch] = v & 15;
            else if (ch < 3) period_[ch] = (period_[ch] & 0x3F0) | (v & 15);
            else { noise_ = v & 7; lfsr_ = 0x8000; }
        } else {
            const int ch = latch_ >> 1;
            if (latch_ & 1) vol_[ch] = v & 15;
            else if (ch < 3) period_[ch] = (period_[ch] & 0x00F) | ((v & 0x3F) << 4);
            else { noise_ = v & 7; lfsr_ = 0x8000; }
        }
    }
    // One output sample; `ticks` is how many internal clocks (clock/16) elapsed.
    int sample(double ticks) {
        static const int kVol[16] = {8191, 6507, 5168, 4105, 3261, 2590, 2057, 1634, 1298, 1031, 819, 650, 517, 410, 326, 0};
        acc_ += ticks;
        int n = int(acc_);
        acc_ -= n;
        for (; n > 0; n--) {
            for (int c = 0; c < 3; c++) {
                if (--count_[c] <= 0) {
                    count_[c] = period_[c] ? period_[c] : 0x400;
                    out_[c] = !out_[c];
                    if (c == 2 && (noise_ & 3) == 3) stepNoise();
                }
            }
            if ((noise_ & 3) != 3 && --count_[3] <= 0) {
                count_[3] = 0x10 << (noise_ & 3);
                stepNoise();
            }
        }
        int s = 0;
        for (int c = 0; c < 3; c++)
            if (period_[c] > 1) s += out_[c] ? kVol[vol_[c]] : -kVol[vol_[c]];
            else s += kVol[vol_[c]];
        s += (lfsr_ & 1) ? kVol[vol_[3]] : -kVol[vol_[3]];
        return s / 4;
    }

    // Saved games: every field as a 32-bit value, in a fixed order.
    template <class F> void fields(F&& f) {
        f(latch_); for (int& v : period_) f(v); for (int& v : count_) f(v); for (int& v : vol_) f(v);
        f(noise_); f(lfsr_);
        for (bool& b : out_) { int v = b; f(v); b = v != 0; }
        { int v = noiseFlip_; f(v); noiseFlip_ = v != 0; }
    }

private:
    void stepNoise() {
        noiseFlip_ = !noiseFlip_;
        if (!noiseFlip_) return;
        const int fb = (noise_ & 4) ? ((lfsr_ ^ (lfsr_ >> 3)) & 1) : (lfsr_ & 1);
        lfsr_ = (lfsr_ >> 1) | (fb << 15);
    }
    int latch_ = 0, period_[3] = {0, 0, 0}, count_[4] = {1, 1, 1, 1}, vol_[4] = {15, 15, 15, 15};
    int noise_ = 0, lfsr_ = 0x8000;
    bool out_[3] = {false, false, false}, noiseFlip_ = false;
    double acc_ = 0;
};

}  // namespace

struct AudioMachine::Impl : ymfm::ymfm_interface {
    const std::vector<u8>& rom;
    u8 ram[0x2000] = {};
    z80 cpu{};
    ymfm::ym2612 fm;
    Psg psg;
    u32 bank = 0;
    bool loaded = false;
    double cycleDebt = 0;
    int frameSamples = 0;
    s32 timer[2] = {-1, -1};
    // Output filter state, per stereo side.
    double prevIn[2] = {0, 0}, lp[2] = {0, 0}, dcIn[2] = {0, 0}, dcOut[2] = {0, 0};

    explicit Impl(const std::vector<u8>& r) : rom(r), fm(*this) {
        z80_init(&cpu);
        cpu.userdata = this;
        cpu.read_byte = [](void* u, uint16_t a) { return static_cast<Impl*>(u)->read(a); };
        cpu.write_byte = [](void* u, uint16_t a, uint8_t v) { static_cast<Impl*>(u)->write(a, v); };
        cpu.port_in = [](z80*, uint8_t) -> uint8_t { return 0xFF; };
        cpu.port_out = [](z80*, uint8_t, uint8_t) {};
        fm.reset();
    }

    u8 read(u16 a) {
        if (a < 0x4000) return ram[a & 0x1FFF];
        if (a < 0x6000) return fm.read_status();
        if (a < 0x8000) return 0xFF;
        const u32 r = (bank << 15) | (a & 0x7FFF);
        return r < rom.size() ? rom[r] : 0xFF;
    }
    void write(u16 a, u8 v) {
        if (a < 0x4000) ram[a & 0x1FFF] = v;
        else if (a < 0x6000) fm.write(a & 3, v);
        else if (a < 0x6100) bank = ((bank >> 1) | (u32(v & 1) << 8)) & 0x1FF;   // nine bits, shifted in
        else if (a == 0x7F11) psg.write(v);
    }

    // FM chip timers, counted in chip clocks.
    void ymfm_set_timer(uint32_t tnum, int32_t duration) override { timer[tnum & 1] = duration; }
    void tickTimers(int clocks) {
        for (int t = 0; t < 2; t++) {
            if (timer[t] < 0) continue;
            timer[t] -= clocks;
            if (timer[t] <= 0) {
                timer[t] = -1;
                m_engine->engine_timer_expired(u32(t));
            }
        }
    }
};

AudioMachine::AudioMachine(const std::vector<u8>& rom) : d(new Impl(rom)) {}
AudioMachine::~AudioMachine() = default;

void AudioMachine::loadDriver(u32 start, u32 end) {
    std::lock_guard<std::mutex> g(lock_);
    std::memset(d->ram, 0, sizeof d->ram);
    for (u32 a = start; a < end && a - start < sizeof d->ram; a++) d->ram[a - start] = d->rom[a];
    void* user = d->cpu.userdata;
    auto rb = d->cpu.read_byte; auto wb = d->cpu.write_byte; auto pi = d->cpu.port_in; auto po = d->cpu.port_out;
    z80_init(&d->cpu);
    d->cpu.userdata = user; d->cpu.read_byte = rb; d->cpu.write_byte = wb; d->cpu.port_in = pi; d->cpu.port_out = po;
    d->fm.reset();
    d->bank = 0;
    d->loaded = true;
}

void AudioMachine::queueByte(u8 b) {
    std::lock_guard<std::mutex> g(lock_);
    u8 idx = d->ram[0x36];
    d->ram[0x1B40 + (idx & 0x3F)] = b;
    d->ram[0x36] = (idx + 1) & 0x3F;
}

void AudioMachine::command(u8 code) {
    queueByte(0xFF);
    queueByte(code);
}

void AudioMachine::render(s16* out, int frames) {
    std::lock_guard<std::mutex> g(lock_);
    static const double kZ80PerSample = 3579545.0 / kSampleRate;
    static const double kPsgPerSample = 3579545.0 / 16.0 / kSampleRate;
    for (int i = 0; i < frames; i++) {
        if (d->loaded) {
            d->cycleDebt += kZ80PerSample;
            const unsigned long startCyc = d->cpu.cyc;
            while (double(d->cpu.cyc - startCyc) < d->cycleDebt) z80_step(&d->cpu);
            d->cycleDebt -= double(d->cpu.cyc - startCyc);
            if (++d->frameSamples >= kSampleRate / 60) {   // the video frame interrupt
                d->frameSamples = 0;
                z80_gen_int(&d->cpu, 0xFF);
            }
        }
        ymfm::ym2612::output_data o;
        d->fm.generate(&o);
        d->tickTimers(144);
        const int p = d->psg.sample(kPsgPerSample);
        // The console's analogue output stage low-pass filters the chips.
        // Without it the raw stream carries tones at half the sample rate
        // (one percussion voice produces exactly that), which would alias
        // when the host resamples. A two-sample average puts a null there,
        // a one-pole filter rolls off the top, and a DC blocker removes the
        // FM chip's constant offset.
        const double in[2] = {double(o.data[0] * 2 + p), double(o.data[1] * 2 + p)};
        int outv[2];
        for (int c = 0; c < 2; c++) {
            const double avg = 0.5 * (in[c] + d->prevIn[c]);
            d->prevIn[c] = in[c];
            d->lp[c] += 0.55 * (avg - d->lp[c]);
            const double y = d->lp[c] - d->dcIn[c] + 0.9995 * d->dcOut[c];
            d->dcIn[c] = d->lp[c];
            d->dcOut[c] = y;
            outv[c] = int(y);
        }
        int l = outv[0], r = outv[1];
        if (l > 32767) l = 32767; if (l < -32768) l = -32768;
        if (r > 32767) r = 32767; if (r < -32768) r = -32768;
        out[2 * i] = s16(l);
        out[2 * i + 1] = s16(r);
    }
}

namespace {

// Saved state is a flat list of little-endian 32-bit values and byte blocks,
// so a file written on one system loads on another.
struct Out {
    std::vector<u8>& b;
    void u32v(u32 v) { for (int i = 0; i < 4; i++) b.push_back(u8(v >> (8 * i))); }
    void bytes(const u8* p, size_t n) { b.insert(b.end(), p, p + n); }
};
struct In {
    const u8* p; size_t left; bool ok = true;
    u32 u32v() {
        if (left < 4) { ok = false; return 0; }
        const u32 v = u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24;
        p += 4; left -= 4;
        return v;
    }
    void bytes(u8* dst, size_t n) {
        if (left < n) { ok = false; return; }
        std::memcpy(dst, p, n);
        p += n; left -= n;
    }
};

// The Z80's registers through one function for both directions.
template <class F> void cpuFields(z80& c, F&& f) {
    auto w = [&](auto& field) { u32 v = u32(field); f(v); field = static_cast<std::remove_reference_t<decltype(field)>>(v); };
    auto bit = [&](bool cur, auto set) { u32 v = cur; f(v); set(v != 0); };
    { u32 v = u32(c.cyc); f(v); c.cyc = v; }
    w(c.pc); w(c.sp); w(c.ix); w(c.iy); w(c.mem_ptr);
    w(c.a); w(c.b); w(c.c); w(c.d); w(c.e); w(c.h); w(c.l);
    w(c.a_); w(c.b_); w(c.c_); w(c.d_); w(c.e_); w(c.h_); w(c.l_); w(c.f_);
    w(c.i); w(c.r);
    bit(c.sf, [&](bool v) { c.sf = v; }); bit(c.zf, [&](bool v) { c.zf = v; }); bit(c.yf, [&](bool v) { c.yf = v; });
    bit(c.hf, [&](bool v) { c.hf = v; }); bit(c.xf, [&](bool v) { c.xf = v; }); bit(c.pf, [&](bool v) { c.pf = v; });
    bit(c.nf, [&](bool v) { c.nf = v; }); bit(c.cf, [&](bool v) { c.cf = v; });
    w(c.iff_delay); w(c.interrupt_mode); w(c.int_data);
    bit(c.iff1, [&](bool v) { c.iff1 = v; }); bit(c.iff2, [&](bool v) { c.iff2 = v; }); bit(c.halted, [&](bool v) { c.halted = v; });
    bit(c.int_pending, [&](bool v) { c.int_pending = v; }); bit(c.nmi_pending, [&](bool v) { c.nmi_pending = v; });
}

constexpr u32 kAudioMagic = 0x31445541;   // "AUD1"

}  // namespace

void AudioMachine::saveState(std::vector<u8>& out) {
    std::lock_guard<std::mutex> g(lock_);
    Out o{out};
    o.u32v(kAudioMagic);
    o.bytes(d->ram, sizeof d->ram);
    cpuFields(d->cpu, [&](u32& v) { o.u32v(v); });
    d->psg.fields([&](int& v) { o.u32v(u32(v)); });
    o.u32v(d->bank); o.u32v(d->loaded); o.u32v(u32(d->frameSamples));
    o.u32v(u32(d->timer[0])); o.u32v(u32(d->timer[1]));
    std::vector<u8> chip;
    ymfm::ymfm_saved_state st(chip, true);
    d->fm.save_restore(st);
    o.u32v(u32(chip.size()));
    o.bytes(chip.data(), chip.size());
}

bool AudioMachine::loadState(const u8* data, size_t size) {
    // Read into copies first, so bad data leaves the running state alone.
    In in{data, size};
    if (in.u32v() != kAudioMagic) return false;
    std::vector<u8> ram(sizeof d->ram);
    in.bytes(ram.data(), ram.size());
    z80 cpu{};
    cpuFields(cpu, [&](u32& v) { v = in.u32v(); });
    Psg psg;
    psg.fields([&](int& v) { v = int(in.u32v()); });
    const u32 bank = in.u32v(), loaded = in.u32v(), frameSamples = in.u32v();
    const s32 t0 = s32(in.u32v()), t1 = s32(in.u32v());
    const u32 chipSize = in.u32v();
    if (!in.ok || chipSize > in.left) return false;
    std::vector<u8> chip(in.p, in.p + chipSize);

    std::lock_guard<std::mutex> g(lock_);
    std::memcpy(d->ram, ram.data(), ram.size());
    cpu.userdata = d->cpu.userdata; cpu.read_byte = d->cpu.read_byte; cpu.write_byte = d->cpu.write_byte;
    cpu.port_in = d->cpu.port_in; cpu.port_out = d->cpu.port_out;
    d->cpu = cpu;
    d->psg = psg;
    d->bank = bank; d->loaded = loaded != 0; d->frameSamples = int(frameSamples);
    d->timer[0] = t0; d->timer[1] = t1;
    d->cycleDebt = 0;
    ymfm::ymfm_saved_state st(chip, false);
    d->fm.save_restore(st);
    for (int c = 0; c < 2; c++) d->prevIn[c] = d->lp[c] = d->dcIn[c] = d->dcOut[c] = 0;
    return true;
}
