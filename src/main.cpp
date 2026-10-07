// SFML shell: window, input, pacing, presentation. Game logic lives in game/.
#include "core/audio.h"
#include "core/machine.h"
#include "game/addr.h"
#include "game/autoplay.h"
#include "game/game.h"
#include "game/interrupts.h"
#include "game/script.h"
#include "game/sound.h"
#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {

std::unique_ptr<AudioMachine> g_audio;

// Translates the game's six sound commands into the driver's queue protocol
// (0x1F6E0A-0x1F6F60).
void connectSound() {
    g_audio = std::make_unique<AudioMachine>(M.rom);
    snd::sink = [](const snd::Command& c) {
        AudioMachine& a = *g_audio;
        switch (c.kind) {
            case snd::Command::Init:
                a.loadDriver(0x1BB94A, 0x1BD1D0);
                a.queueByte(0xFF);
                a.queueByte(0x0B);
                for (int i = 0; i < 4; i++) {   // four 24-bit data bank addresses, low byte first
                    a.queueByte(u8(c.arg[i]));
                    a.queueByte(u8(c.arg[i] >> 8));
                    a.queueByte(u8(c.arg[i] >> 16));
                }
                break;
            case snd::Command::StartSequence: a.command(0x10); a.queueByte(u8(c.arg[0])); break;
            case snd::Command::StopSequence: a.command(0x12); a.queueByte(u8(c.arg[0])); break;
            case snd::Command::PauseAll: a.command(0x0C); break;
            case snd::Command::ResumeAll: a.command(0x0D); break;
            case snd::Command::StopAll: a.command(0x16); break;
        }
    };
}

class AudioStream : public sf::SoundStream {
public:
    AudioStream() { initialize(2, AudioMachine::kSampleRate, {sf::SoundChannel::FrontLeft, sf::SoundChannel::FrontRight}); }
private:
    bool onGetData(Chunk& data) override {
        g_audio->render(buf_, kFrames);
        data.samples = buf_;
        data.sampleCount = kFrames * 2;
        return true;
    }
    void onSeek(sf::Time) override {}
    static constexpr int kFrames = 1024;
    s16 buf_[kFrames * 2];
};

struct Options {
    std::string rom;            // empty: look for it next to the program
    int scale = 3;
    long frames = -1;           // headless: stop after this many frames
    std::string dumpDir;        // headless: write frame/RAM dumps here
    std::vector<long> dumpAt;   // frame numbers to dump
    bool headless = false;
    bool fullscreen = false;
    bool mute = false;
    bool solve = false;         // headless: search for a way to the episode's ending
    std::string replay;         // headless: run this list of actions first (or only, without --solve)
    std::string wav;            // headless: write the rendered sound here
    std::string scriptTrace;    // headless: log every executed script instruction here
    struct MouseEv { long frame; int x, y; bool left, right; };
    std::vector<MouseEv> mouse;                 // headless: pointer events
    std::vector<std::pair<long, u8>> presses;   // headless: frame -> pad bits held for 5 frames
};

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--rom") o.rom = next();
        else if (a == "--fullscreen") o.fullscreen = true;
        else if (a == "--mute") o.mute = true;
        else if (a == "--solve") { o.solve = true; o.headless = true; if (o.frames < 0) o.frames = 2000000000L; }
        else if (a == "--replay") { o.replay = next(); o.headless = true; if (o.frames < 0) o.frames = 2000000000L; }
        else if (a == "--room") M.debugRoom = std::atoi(next().c_str());
        else if (a == "--poke") {   // testing aid: ADDR=VALUE in hex, applied with --room
            const std::string s = next();
            const size_t eq = s.find('=');
            M.debugPokes.push_back({u32(std::strtoul(s.substr(0, eq).c_str(), nullptr, 16)), u8(std::strtoul(s.substr(eq + 1).c_str(), nullptr, 16))});
        }
        else if (a == "--wav") o.wav = next();
        else if (a == "--script-trace") o.scriptTrace = next();
        else if (a == "--scale") o.scale = std::max(1, std::atoi(next().c_str()));
        else if (a == "--frames") { o.frames = std::atol(next().c_str()); o.headless = true; }
        else if (a == "--dump-dir") o.dumpDir = next();
        else if (a == "--dump-at") {
            std::string s = next(); size_t p = 0;
            while (p < s.size()) { size_t q = s.find(',', p); if (q == std::string::npos) q = s.size(); o.dumpAt.push_back(std::atol(s.substr(p, q - p).c_str())); p = q + 1; }
        } else if (a == "--mouse") {
            // e.g. 14600:130:60,14700:100:120:L
            std::string s = next(); size_t p = 0;
            while (p < s.size()) {
                size_t q = s.find(',', p); if (q == std::string::npos) q = s.size();
                const std::string item = s.substr(p, q - p);
                long f = 0; int x = 0, y = 0; char c = 0;
                std::sscanf(item.c_str(), "%ld:%d:%d:%c", &f, &x, &y, &c);
                o.mouse.push_back({f, x, y, c == 'L', c == 'R'});
                p = q + 1;
            }
        } else if (a == "--press") {
            // e.g. 1900:S,2300:B  (U D L R A B C S)
            std::string s = next(); size_t p = 0;
            while (p < s.size()) {
                size_t q = s.find(',', p); if (q == std::string::npos) q = s.size();
                const std::string item = s.substr(p, q - p);
                const size_t c = item.find(':');
                u8 bits = 0;
                for (char ch : item.substr(c + 1)) {
                    switch (ch) {
                        case 'U': bits |= PadUp; break; case 'D': bits |= PadDown; break;
                        case 'L': bits |= PadLeft; break; case 'R': bits |= PadRight; break;
                        case 'A': bits |= PadA; break; case 'B': bits |= PadB; break;
                        case 'C': bits |= PadC; break; case 'S': bits |= PadStart; break;
                    }
                }
                o.presses.push_back({std::atol(item.substr(0, c).c_str()), bits});
                p = q + 1;
            }
        } else if (a[0] != '-') o.rom = a;
    }
    return o;
}

// One presented frame: interrupt work, a slice of game logic, then the picture.
void stepFrame(std::vector<u16>& frame) {
    irq::vblank();
    M.runGameSlice();
    M.vdp.renderFrame(frame, irq::beforeLine);
    M.frameCounter++;
}

void writeDump(const std::string& dir, long n, const std::vector<u16>& frame) {
    char name[64];
    std::snprintf(name, sizeof name, "/f%06ld.bin", n);
    std::ofstream f(dir + name, std::ios::binary);
    const int w = M.vdp.width();
    u8 hdr[4] = {u8(w >> 8), u8(w), 0, 224};
    f.write(reinterpret_cast<const char*>(hdr), 4);
    for (int y = 0; y < 224; y++)
        for (int x = 0; x < w; x++) {
            const u16 c = frame[size_t(y) * Vdp::kMaxWidth + x];
            const char px[3] = {char((c >> 1) & 7), char((c >> 5) & 7), char((c >> 9) & 7)};
            f.write(px, 3);
        }
    f.write(reinterpret_cast<const char*>(M.ram), sizeof M.ram);
}

int runHeadless(const Options& o) {
    std::FILE* trace = o.scriptTrace.empty() ? nullptr : std::fopen(o.scriptTrace.c_str(), "w");
    if (trace) vm::trace = [trace](u32 pc, u16 op, bool exec) { if (exec) std::fprintf(trace, "%06X %02X\n", pc, op); };
    std::vector<u16> frame;
    std::vector<s16> pcm;
    const int perFrame = AudioMachine::kSampleRate / 60;
    if (!o.replay.empty()) autoplay::loadReplay(o.replay, o.solve);
    long solveFrom = 0; u32 lastIdle = 0; long lastIdleFrame = 0;
    for (long n = 1; n <= o.frames; n++) {
        M.pad1 = 0;
        for (const auto& pr : o.presses) if (n >= pr.first && n < pr.first + 5) M.pad1 |= pr.second;
        M.skipRequested = false;
        for (const auto& pr : o.presses) if (n == pr.first) M.skipRequested = true;
        for (const auto& me : o.mouse) {
            if (n == me.frame) { M.mouseEnabled = true; M.mouseMoved = true; M.mouseActive = true; M.mouseX = me.x; M.mouseY = me.y; }
            const bool held = n >= me.frame + 2 && n < me.frame + 7;
            const bool title = M.rd32(VBlankHandler) == irq::VblTitle;
            if (me.left) { if (held && !title) M.pad1 |= PadB; if (n == me.frame + 2) M.mouseLeft = true; if (n == me.frame + 7) M.mouseLeft = false; }
            if (me.right) { if (held && !title) M.pad1 |= PadA; if (n == me.frame + 2) M.mouseRight = true; if (n == me.frame + 7) M.mouseRight = false; }
        }
        if (o.solve || !o.replay.empty()) {
            // No picture is needed while searching.
            irq::vblank();
            M.runGameSlice();
            M.frameCounter++;
            if (!autoplay::active && autoplay::idleCalls > 0 && solveFrom == 0) solveFrom = n + 200;
            if (solveFrom && n == solveFrom) { autoplay::active = true; std::fprintf(stderr, "solver: starting at frame %ld\n", n); }
            if (autoplay::idleCalls != lastIdle) { lastIdle = autoplay::idleCalls; lastIdleFrame = n; }
            if (autoplay::active && n - lastIdleFrame == 6000) autoplay::abortRequested = true;
            if (autoplay::active && n - lastIdleFrame > 60000) {
                std::fprintf(stderr, "solver: no idle point for 60000 frames; the game is waiting on something\n");
                M.vdp.renderFrame(frame, irq::beforeLine);
                if (!o.dumpDir.empty()) writeDump(o.dumpDir, n, frame);
                autoplay::stalled();
                std::fprintf(stderr, "solver: %s\n", autoplay::result.c_str());
                break;
            }
            if (autoplay::done) { std::fprintf(stderr, "solver: %s\n", autoplay::result.c_str()); break; }
            continue;
        }
        stepFrame(frame);
        if (!o.wav.empty()) {
            const size_t at = pcm.size();
            pcm.resize(at + size_t(perFrame) * 2);
            g_audio->render(&pcm[at], perFrame);
        }
        for (long d : o.dumpAt) if (d == n && !o.dumpDir.empty()) writeDump(o.dumpDir, n, frame);
    }
    if (!o.wav.empty()) {
        std::ofstream f(o.wav, std::ios::binary);
        const u32 bytes = u32(pcm.size() * 2), rate = AudioMachine::kSampleRate;
        auto u32le = [&](u32 v) { f.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16le = [&](u16 v) { f.write(reinterpret_cast<const char*>(&v), 2); };
        f.write("RIFF", 4); u32le(36 + bytes); f.write("WAVEfmt ", 8); u32le(16); u16le(1); u16le(2);
        u32le(rate); u32le(rate * 4); u16le(4); u16le(16); f.write("data", 4); u32le(bytes);
        f.write(reinterpret_cast<const char*>(pcm.data()), bytes);
    }
    M.shutdown();
    if (trace) std::fclose(trace);
    g_audio.reset();
    return 0;
}

u8 readPad(const sf::RenderWindow& window) {
    if (!window.hasFocus()) return 0;
    using K = sf::Keyboard::Key;
    auto down = [](K k) { return sf::Keyboard::isKeyPressed(k); };
    u8 p = 0;
    if (down(K::Up)) p |= PadUp;
    if (down(K::Down)) p |= PadDown;
    if (down(K::Left)) p |= PadLeft;
    if (down(K::Right)) p |= PadRight;
    if (down(K::A) || down(K::Z)) p |= PadA;
    if (down(K::S) || down(K::X)) p |= PadB;
    if (down(K::D) || down(K::C)) p |= PadC;
    if (down(K::Enter)) p |= PadStart;
    return p;
}

// The largest 4:3 rectangle that fits the window, centred. The console shows
// its 256-pixel mode across a 4:3 picture, so the picture is always 4:3.
sf::FloatRect pictureRect(sf::Vector2u win) {
    float w = float(win.x), h = float(win.y);
    float pw = w, ph = w * 3.f / 4.f;
    if (ph > h) { ph = h; pw = h * 4.f / 3.f; }
    return sf::FloatRect({(w - pw) / 2.f, (h - ph) / 2.f}, {pw, ph});
}

void openWindow(sf::RenderWindow& window, bool fullscreen, int scale) {
    if (fullscreen) {
        // Desktop mode: whatever resolution the display is running at.
        window.create(sf::VideoMode::getDesktopMode(), "Scooby-Doo Mystery", sf::State::Fullscreen);
    } else {
        const unsigned h = 224u * unsigned(scale);
        window.create(sf::VideoMode({h * 4 / 3, h}), "Scooby-Doo Mystery", sf::Style::Default, sf::State::Windowed);
    }
    window.setFramerateLimit(60);
    window.setKeyRepeatEnabled(false);
    window.setMouseCursorVisible(false);   // the game draws its own pointer
}

int runWindowed(const Options& o) {
    sf::RenderWindow window;
    bool fullscreen = o.fullscreen;
    openWindow(window, fullscreen, o.scale);

    std::unique_ptr<AudioStream> stream;
    if (!o.mute) { stream = std::make_unique<AudioStream>(); stream->play(); }

    sf::Texture texture(sf::Vector2u{Vdp::kMaxWidth, Vdp::kHeight});
    texture.setSmooth(false);
    std::vector<u16> frame;
    std::vector<std::uint8_t> rgba(size_t(Vdp::kMaxWidth) * Vdp::kHeight * 4);

    while (window.isOpen()) {
        bool toggle = false;
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) window.close();
            if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>()) M.mouseWheel += wheel->delta > 0 ? 1 : wheel->delta < 0 ? -1 : 0;
            if (const auto* k = event->getIf<sf::Event::KeyPressed>()) {
                using K = sf::Keyboard::Key;
                if (k->code == K::Escape) { if (fullscreen) toggle = true; else window.close(); }
                if (k->code == K::F11 || k->code == K::F || (k->code == K::Enter && k->alt)) toggle = true;
            }
        }
        if (toggle) {
            fullscreen = !fullscreen;
            openWindow(window, fullscreen, o.scale);
        }
        if (!window.isOpen()) break;

        const sf::Vector2u ws = window.getSize();
        const sf::FloatRect pic = pictureRect(ws);
        // Draw in window pixels so resizing never stretches the picture.
        window.setView(sf::View(sf::FloatRect({0.f, 0.f}, {float(ws.x), float(ws.y)})));

        M.pad1 = readPad(window);
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LAlt) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt))
            M.pad1 &= u8(~PadStart);   // Alt+Enter is the fullscreen toggle, not Start
        // Mouse: position drives the pointer, left acts, right cancels,
        // middle swaps verbs and inventory. On the title menus a click confirms.
        {
            const sf::Vector2i mp = sf::Mouse::getPosition(window);
            const float fx = (float(mp.x) - pic.position.x) / pic.size.x;
            const float fy = (float(mp.y) - pic.position.y) / pic.size.y;
            const bool inside = fx >= 0.f && fy >= 0.f && fx < 1.f && fy < 1.f && window.hasFocus();
            const bool title = M.rd32(VBlankHandler) == irq::VblTitle;
            if (inside) {
                const int gx = int(fx * float(M.vdp.width())), gy = int(fy * 224.f);
                if (gx != M.mouseX || gy != M.mouseY) { M.mouseMoved = true; M.mouseActive = true; }
                M.mouseX = gx; M.mouseY = gy; M.mouseEnabled = true;
                M.mouseLeft = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
                M.mouseRight = sf::Mouse::isButtonPressed(sf::Mouse::Button::Right);
                M.mouseMiddle = sf::Mouse::isButtonPressed(sf::Mouse::Button::Middle);
                // The title menus read the mouse themselves (menu.cpp) and
                // show the system pointer; in the game it becomes pad buttons.
                if (!title) {
                    if (M.mouseLeft) M.pad1 |= PadB;
                    if (M.mouseRight) M.pad1 |= PadA;
                    if (M.mouseMiddle) M.pad1 |= PadC;
                }
            } else {
                M.mouseLeft = M.mouseRight = M.mouseMiddle = false;
            }
            static bool shown = false;
            if (title != shown) { shown = title; window.setMouseCursorVisible(title); }
        }
        {
            static u8 lastPad = 0;
            if (M.pad1 & ~lastPad) M.skipRequested = true;
            lastPad = M.pad1;
        }
        stepFrame(frame);
        M.skipRequested = false;

        const int w = M.vdp.width();
        for (size_t i = 0; i < frame.size(); i++) {
            const u32 c = Vdp::toRgba(frame[i]);
            std::memcpy(&rgba[i * 4], &c, 4);
        }
        texture.update(rgba.data());
        sf::Sprite sprite(texture, sf::IntRect({0, 0}, {w, Vdp::kHeight}));
        sprite.setPosition(pic.position);
        sprite.setScale({pic.size.x / float(w), pic.size.y / float(Vdp::kHeight)});
        window.clear(sf::Color::Black);
        window.draw(sprite);
        window.display();
        if (M.gameFinished()) window.close();
    }
    if (stream) stream->stop();
    M.shutdown();
    return 0;
}

}  // namespace

namespace {

namespace fs = std::filesystem;

// The folder the program file is in.
fs::path programFolder(const char* argv0) {
    std::error_code ec;
#if defined(_WIN32)
    wchar_t buf[4096];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
    if (n > 0 && n < 4096) return fs::path(buf).parent_path();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0) {
        const fs::path p = fs::canonical(buf, ec);
        if (!ec) return p.parent_path();
    }
#else
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return self.parent_path();
#endif
    const fs::path p = fs::absolute(argv0 ? argv0 : ".", ec);
    return ec ? fs::current_path(ec) : p.parent_path();
}

// Looks for the cartridge image by content, whatever it is called: any 2 MB
// file in these folders that the loader accepts.
std::string findRom(const std::vector<fs::path>& folders) {
    std::error_code ec;
    for (const fs::path& dir : folders) {
        std::vector<fs::path> files;
        for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (it->is_regular_file(e2) && it->file_size(e2) == 0x200000) files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& f : files)
            if (M.loadRom(f.string()).empty()) return f.string();
    }
    return "";
}

// Shown in a dialog as well as on the terminal, because someone who
// double-clicks the program never sees the terminal.
void tellUser(const std::string& title, const std::string& text, bool dialog) {
    std::fprintf(stderr, "%s\n\n%s\n", title.c_str(), text.c_str());
    if (!dialog) return;
#if defined(_WIN32)
    MessageBoxA(nullptr, text.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
#else
    // The text goes through the environment so nothing in it is read as a command.
    setenv("SCOOBY_MSG_TITLE", title.c_str(), 1);
    setenv("SCOOBY_MSG_TEXT", text.c_str(), 1);
#if defined(__APPLE__)
    const int rc = std::system("osascript -e 'display dialog (system attribute \"SCOOBY_MSG_TEXT\") with title (system attribute \"SCOOBY_MSG_TITLE\") buttons {\"OK\"} default button 1' >/dev/null 2>&1");
#else
    const int rc = std::system("(zenity --info --no-markup --width=480 --title=\"$SCOOBY_MSG_TITLE\" --text=\"$SCOOBY_MSG_TEXT\" "
                               "|| kdialog --title \"$SCOOBY_MSG_TITLE\" --msgbox \"$SCOOBY_MSG_TEXT\" "
                               "|| xmessage -center \"$SCOOBY_MSG_TEXT\") >/dev/null 2>&1");
#endif
    (void)rc;
#endif
}

#if defined(__APPLE__)
// macOS may run a downloaded app from a hidden, read-only copy, so "the
// folder the program is in" is not somewhere a person can put a file. There
// the ROM is picked in a file dialog once and its location remembered.
fs::path rememberedRomFile() {
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / "Library" / "Application Support" / "scooby" / "rom-path.txt";
}

std::string runAndRead(const char* command) {
    std::string out;
    if (FILE* p = popen(command, "r")) {
        char buf[512];
        while (std::fgets(buf, sizeof buf, p)) out += buf;
        pclose(p);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

// Asks for the ROM until a usable one is chosen or the person gives up.
std::string chooseRom() {
    for (;;) {
        const std::string picked = runAndRead(
            "osascript -e 'POSIX path of (choose file with prompt \"Choose your ROM of Scooby-Doo Mystery (USA) for the Sega Genesis. "
            "It must be the plain, unzipped 2 MB file. No part of the game is included with this program.\")' 2>/dev/null");
        if (picked.empty()) return "";
        if (M.loadRom(picked).empty()) {
            std::error_code ec;
            fs::create_directories(rememberedRomFile().parent_path(), ec);
            std::ofstream(rememberedRomFile()) << picked << "\n";
            return picked;
        }
        setenv("SCOOBY_MSG_TEXT", (picked + "\n\nThis file cannot be used. It must be the unzipped 2 MB ROM of the USA release.").c_str(), 1);
        const std::string again = runAndRead("osascript -e 'button returned of (display dialog (system attribute \"SCOOBY_MSG_TEXT\") "
                                             "with title \"Scooby-Doo Mystery\" buttons {\"Quit\", \"Choose another\"} default button 2)' 2>/dev/null");
        if (again != "Choose another") return "";
    }
}
#endif

}  // namespace

int main(int argc, char** argv) {
    Options o = parse(argc, argv);
    if (o.rom.empty()) {
        // No ROM named: look beside the program, in the folder it was started
        // from, and in a `rom` folder in either (or one level up, which is
        // where it sits when the program is in `build/`).
        std::error_code ec;
        const fs::path exe = programFolder(argc > 0 ? argv[0] : nullptr), cwd = fs::current_path(ec);
        std::vector<fs::path> folders{exe, exe / "rom", exe.parent_path() / "rom", cwd, cwd / "rom"};
        // Inside a macOS app bundle the natural place is beside the .app.
        if (exe.filename() == "MacOS" && exe.parent_path().filename() == "Contents") {
            const fs::path beside = exe.parent_path().parent_path().parent_path();
            folders.insert(folders.begin(), {beside, beside / "rom"});
        }
        o.rom = findRom(folders);
#if defined(__APPLE__)
        if (o.rom.empty()) {
            std::string saved;
            std::ifstream in(rememberedRomFile());
            if (std::getline(in, saved) && M.loadRom(saved).empty()) o.rom = saved;
        }
        if (o.rom.empty() && !o.headless) {
            o.rom = chooseRom();
            if (o.rom.empty()) return 1;
        }
#endif
        if (o.rom.empty()) {
            tellUser("Scooby-Doo Mystery: ROM not found",
                     "This program needs your own copy of the game to run.\n\n"
                     "Put the ROM of Scooby-Doo Mystery (USA) for the Sega Genesis in this folder:\n\n    " + exe.string() + "\n\n"
                     "and start the program again. The file can have any name. It must be the plain, "
                     "unzipped 2 MB cartridge image (usually ending in .md, .bin or .gen).\n\n"
                     "You can also drag the ROM file onto the program, or give its path as an argument.",
                     !o.headless);
            return 1;
        }
    }
    const std::string err = M.loadRom(o.rom);
    if (!err.empty()) {
        std::string why = err;
        if (err.rfind("cannot open", 0) == 0) why = "The file could not be opened. Check the path.";
        else if (err.rfind("unexpected ROM size", 0) == 0) why = "It is not a plain 2 MB cartridge image. If it is a zip, unzip it first.";
        else if (err.rfind("checksum", 0) == 0 || err.rfind("not a", 0) == 0) why = "It is not the USA release of Scooby-Doo Mystery, or the file is damaged or modified.";
        tellUser("Scooby-Doo Mystery: this ROM cannot be used", o.rom + "\n\n" + why, !o.headless);
        return 1;
    }
    M.vdp.reset();
    connectSound();
    M.start(game::entry);
    return o.headless ? runHeadless(o) : runWindowed(o);
}
