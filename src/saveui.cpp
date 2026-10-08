#include "saveui.h"
#include "font5x7.h"
#include "game/savestate.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

namespace fs = std::filesystem;

// The dialog is laid out on a 320x240 grid that is stretched over the 4:3
// picture, so it scales with the window like the game does.
constexpr float kW = 320, kH = 240;
constexpr float kBoxX = 24, kBoxY = 28, kBoxW = 272, kBoxH = 184;
constexpr float kListX = 34, kListY = 52, kListW = 186, kRowH = 16;
constexpr float kButtonX = 230, kButtonY = 56, kButtonW = 56, kButtonH = 18, kButtonGap = 24;
constexpr size_t kMaxName = 24;

const sf::Color kShade(0, 0, 0, 150), kBox(44, 40, 104), kEdgeLight(170, 166, 236), kEdgeDark(16, 14, 48);
const sf::Color kPanel(18, 16, 56), kRowPick(104, 96, 216), kRowHover(60, 56, 140);
const sf::Color kButton(84, 78, 172), kButtonHover(124, 116, 228), kButtonOff(56, 54, 96);
const sf::Color kText(255, 255, 255), kTextDim(150, 148, 190), kTitle(255, 236, 120), kNote(255, 150, 130);

struct Canvas {
    sf::VertexArray v{sf::PrimitiveType::Triangles};
    void rect(float x, float y, float w, float h, sf::Color c) {
        const sf::Vector2f a{x, y}, b{x + w, y}, d{x, y + h}, e{x + w, y + h};
        for (const sf::Vector2f& p : {a, b, e, a, e, d}) v.append(sf::Vertex{p, c});
    }
    // A box with a light top-left edge and a dark bottom-right one.
    void bevel(float x, float y, float w, float h, sf::Color fill, bool sunk = false) {
        rect(x, y, w, h, fill);
        const sf::Color tl = sunk ? kEdgeDark : kEdgeLight, br = sunk ? kEdgeLight : kEdgeDark;
        rect(x, y, w, 1, tl); rect(x, y, 1, h, tl);
        rect(x, y + h - 1, w, 1, br); rect(x + w - 1, y, 1, h, br);
    }
    void text(float x, float y, const std::string& s, sf::Color c) {
        for (unsigned char ch : s) {
            if (ch >= 32 && ch < 127) {
                const unsigned char* g = font5x7::kGlyphs[ch - 32];
                for (int r = 0; r < font5x7::kHeight; r++)
                    for (int k = 0; k < font5x7::kWidth; k++)
                        if (g[r] & (0x10 >> k)) rect(x + float(k), y + float(r), 1, 1, c);
            }
            x += font5x7::kAdvance;
        }
    }
    static float width(const std::string& s) { return float(s.size()) * font5x7::kAdvance - 1; }
    void centred(float cx, float y, const std::string& s, sf::Color c) { text(float(int(cx - width(s) / 2)), y, s, c); }
};

fs::path slotFile(int slot) { return fs::path(SaveDialog::folder()) / ("slot" + std::to_string(slot + 1) + ".sav"); }

std::vector<u8> readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<u8>((std::istreambuf_iterator<char>(f)), {});
}

}  // namespace

std::string SaveDialog::folder() {
    fs::path base;
#if defined(_WIN32)
    if (const char* v = std::getenv("APPDATA")) base = v;
#elif defined(__APPLE__)
    if (const char* v = std::getenv("HOME")) base = fs::path(v) / "Library" / "Application Support";
#else
    if (const char* v = std::getenv("XDG_DATA_HOME"); v && *v) base = v;
    else if (const char* h = std::getenv("HOME")) base = fs::path(h) / ".local" / "share";
#endif
    if (base.empty()) base = ".";
    return (base / "scooby" / "saves").string();
}

void SaveDialog::readSlots() {
    for (int i = 0; i < kSlots; i++) {
        savestate::Info info;
        slots_[i].used = savestate::read(readFile(slotFile(i)), info);
        slots_[i].name = slots_[i].used ? info.name : "";
    }
}

void SaveDialog::open(std::vector<u8> snapshot) {
    snapshot_ = std::move(snapshot);
    readSlots();
    open_ = true;
    result_ = Result::None;
    note_.clear();
    loadData.clear();
    enter(Mode::Main);
}

void SaveDialog::enter(Mode m) {
    mode_ = m;
    selected_ = -1;
    edit_.clear();
    lastClickSlot_ = -1;
    if (m == Mode::Save) {   // start on the first free slot
        selected_ = 0;
        for (int i = 0; i < kSlots; i++)
            if (!slots_[i].used) { selected_ = i; break; }
        edit_ = slots_[selected_].name;
    }
    blink_.restart();
}

void SaveDialog::close(Result r) {
    open_ = false;
    result_ = r;
    snapshot_.clear();
}

SaveDialog::Result SaveDialog::takeResult() {
    const Result r = result_;
    result_ = Result::None;
    return r;
}

std::vector<std::string> SaveDialog::buttons() const {
    switch (mode_) {
        case Mode::Main: return {"Save", "Load", "Play", "Quit"};
        case Mode::ConfirmQuit: return {"Yes", "No"};
        default: return {"OK", "Cancel"};
    }
}

bool SaveDialog::buttonEnabled(int i) const {
    if (mode_ == Mode::Main) {
        if (i == 0) return !snapshot_.empty();
        if (i == 1) { for (const Slot& s : slots_) if (s.used) return true; return false; }
    }
    if (mode_ == Mode::Load && i == 0) return selected_ >= 0 && slots_[selected_].used;
    return true;
}

int SaveDialog::buttonAt(float x, float y) const {
    const int n = int(buttons().size());
    for (int i = 0; i < n; i++) {
        float bx = kButtonX, by = kButtonY + float(i) * kButtonGap;
        if (mode_ == Mode::ConfirmQuit) { bx = 160 - kButtonW - 6 + float(i) * (kButtonW + 12); by = 128; }
        if (x >= bx && x < bx + kButtonW && y >= by && y < by + kButtonH) return i;
    }
    return -1;
}

int SaveDialog::slotAt(float x, float y) const {
    if (mode_ == Mode::ConfirmQuit || x < kListX || x >= kListX + kListW || y < kListY + 3) return -1;
    const int i = int((y - kListY - 3) / kRowH);
    return i < kSlots ? i : -1;
}

void SaveDialog::confirm() {
    if (mode_ == Mode::Save && selected_ >= 0 && !snapshot_.empty()) {
        std::string name = edit_;
        while (!name.empty() && name.back() == ' ') name.pop_back();
        if (name.empty()) name = "Game " + std::to_string(selected_ + 1);
        savestate::setName(snapshot_, name);
        std::error_code ec;
        fs::create_directories(folder(), ec);
        std::ofstream f(slotFile(selected_), std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(snapshot_.data()), std::streamsize(snapshot_.size()));
        f.close();
        if (!f) { note_ = "Could not write the saved game."; return; }
        close(Result::Resume);
    } else if (mode_ == Mode::Load && selected_ >= 0 && slots_[selected_].used) {
        std::vector<u8> data = readFile(slotFile(selected_));
        savestate::Info info;
        if (!savestate::read(data, info)) { note_ = "That saved game cannot be read."; return; }
        loadData = std::move(data);
        loadEpisode = info.episode;
        close(Result::Load);
    }
}

void SaveDialog::click(float x, float y) {
    note_.clear();
    const int b = buttonAt(x, y);
    if (b >= 0 && buttonEnabled(b)) {
        switch (mode_) {
            case Mode::Main:
                if (b == 0) enter(Mode::Save);
                else if (b == 1) enter(Mode::Load);
                else if (b == 2) close(Result::Resume);
                else enter(Mode::ConfirmQuit);
                break;
            case Mode::ConfirmQuit:
                if (b == 0) close(Result::Quit); else enter(Mode::Main);
                break;
            default:
                if (b == 0) confirm(); else enter(Mode::Main);
        }
        return;
    }
    const int s = slotAt(x, y);
    if (s < 0) return;
    if (mode_ == Mode::Save) {
        if (s != selected_) { selected_ = s; edit_ = slots_[s].name; }
    } else if (mode_ == Mode::Load && slots_[s].used) {
        const bool twice = s == lastClickSlot_ && lastClick_.getElapsedTime().asMilliseconds() < 450;
        selected_ = s;
        lastClickSlot_ = s;
        lastClick_.restart();
        if (twice) confirm();
    }
}

void SaveDialog::onEvent(const sf::Event& event, const sf::FloatRect& pic) {
    if (!open_) return;
    auto toGrid = [&](sf::Vector2i p) {
        mouseX_ = (float(p.x) - pic.position.x) / pic.size.x * kW;
        mouseY_ = (float(p.y) - pic.position.y) / pic.size.y * kH;
    };
    if (const auto* m = event.getIf<sf::Event::MouseMoved>()) toGrid(m->position);
    if (const auto* m = event.getIf<sf::Event::MouseButtonPressed>()) {
        toGrid(m->position);
        if (m->button == sf::Mouse::Button::Left) click(mouseX_, mouseY_);
    }
    if (const auto* t = event.getIf<sf::Event::TextEntered>()) {
        if (mode_ == Mode::Save && selected_ >= 0 && t->unicode >= 32 && t->unicode < 127 && edit_.size() < kMaxName) {
            edit_.push_back(char(t->unicode));
            blink_.restart();
        }
    }
    if (const auto* k = event.getIf<sf::Event::KeyPressed>()) {
        using K = sf::Keyboard::Key;
        const K c = k->code;
        if (c == K::Escape) {
            if (mode_ == Mode::Main) close(Result::Resume); else enter(Mode::Main);
        } else if (c == K::F5 && mode_ == Mode::Main) {
            close(Result::Resume);
        } else if (c == K::Enter) {
            if (mode_ == Mode::ConfirmQuit) close(Result::Quit); else confirm();
        } else if (c == K::Backspace && mode_ == Mode::Save) {
            if (!edit_.empty()) edit_.pop_back();
        } else if (mode_ == Mode::ConfirmQuit && (c == K::Y || c == K::N)) {
            if (c == K::Y) close(Result::Quit); else enter(Mode::Main);
        } else if ((c == K::Up || c == K::Down) && (mode_ == Mode::Save || mode_ == Mode::Load)) {
            // Step to the next slot that can be chosen in this mode.
            const int step = c == K::Up ? -1 : 1;
            for (int i = selected_ + step; i >= 0 && i < kSlots; i += step)
                if (mode_ == Mode::Save || slots_[i].used) {
                    selected_ = i;
                    if (mode_ == Mode::Save) edit_ = slots_[i].name;
                    break;
                }
            if (selected_ < 0 && mode_ == Mode::Load)
                for (int i = 0; i < kSlots; i++)
                    if (slots_[i].used) { selected_ = i; break; }
        }
    }
}

void SaveDialog::draw(sf::RenderTarget& target, const sf::FloatRect& pic) {
    if (!open_) return;
    Canvas c;
    c.rect(0, 0, kW, kH, kShade);
    c.bevel(kBoxX, kBoxY, kBoxW, kBoxH, kBox);

    if (mode_ == Mode::ConfirmQuit) {
        c.centred(160, 100, "Are you sure you want to quit?", kTitle);
    } else {
        const char* title = mode_ == Mode::Save ? "Name your SAVE game" : mode_ == Mode::Load ? "Select a game to LOAD" : "Saved games";
        c.centred(kListX + kListW / 2, 38, title, kTitle);
        c.bevel(kListX, kListY, kListW, kRowH * kSlots + 6, kPanel, true);
        const int hover = slotAt(mouseX_, mouseY_);
        for (int i = 0; i < kSlots; i++) {
            const float y = kListY + 3 + float(i) * kRowH;
            const bool pickable = mode_ == Mode::Save || (mode_ == Mode::Load && slots_[i].used);
            if (i == selected_) c.rect(kListX + 2, y, kListW - 4, kRowH, kRowPick);
            else if (i == hover && pickable) c.rect(kListX + 2, y, kListW - 4, kRowH, kRowHover);
            std::string line = std::to_string(i + 1) + ". ";
            if (mode_ == Mode::Save && i == selected_) {
                line += edit_;
                if (blink_.getElapsedTime().asMilliseconds() / 400 % 2 == 0) line += "_";
            } else {
                line += slots_[i].name;
            }
            c.text(kListX + 6, y + 4, line, slots_[i].used || i == selected_ ? kText : kTextDim);
        }
    }

    const std::vector<std::string> names = buttons();
    const int over = buttonAt(mouseX_, mouseY_);
    for (int i = 0; i < int(names.size()); i++) {
        float bx = kButtonX, by = kButtonY + float(i) * kButtonGap;
        if (mode_ == Mode::ConfirmQuit) { bx = 160 - kButtonW - 6 + float(i) * (kButtonW + 12); by = 128; }
        const bool on = buttonEnabled(i);
        c.bevel(bx, by, kButtonW, kButtonH, !on ? kButtonOff : i == over ? kButtonHover : kButton);
        c.centred(bx + kButtonW / 2, by + 6, names[size_t(i)], on ? kText : kTextDim);
    }
    if (!note_.empty()) c.centred(160, kBoxY + kBoxH - 12, note_, kNote);

    // Map the 320x240 grid onto the picture rectangle.
    sf::Transform t;
    t.translate(pic.position).scale({pic.size.x / kW, pic.size.y / kH});
    target.draw(c.v, sf::RenderStates(t));
}
