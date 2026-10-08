// The save and load dialog opened with F5. It is laid out the way the
// classic point-and-click adventures did it: a list of numbered slots on
// the left, and Save, Load, Play and Quit buttons on the right. Drawn by
// the host over the paused game; it does not touch the game's own screen.
#pragma once
#include "core/types.h"
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

class SaveDialog {
public:
    enum class Result { None, Resume, Quit, Load };

    // `snapshot` is the state to write if the player saves; empty when the
    // game cannot be saved at this moment (title screens).
    void open(std::vector<u8> snapshot);
    bool isOpen() const { return open_; }

    void onEvent(const sf::Event& event, const sf::FloatRect& picture);
    void draw(sf::RenderTarget& target, const sf::FloatRect& picture);

    // What the player decided, once. After Load, `loadData` holds the file.
    Result takeResult();
    std::vector<u8> loadData;
    int loadEpisode = 0;

    static std::string folder();   // where saved games are kept

private:
    enum class Mode { Main, Save, Load, ConfirmQuit };
    static constexpr int kSlots = 9;
    struct Slot { bool used = false; std::string name; };

    void readSlots();
    void enter(Mode m);
    void click(float x, float y);
    void confirm();     // OK in Save and Load
    void close(Result r);
    int buttonAt(float x, float y) const;
    int slotAt(float x, float y) const;
    std::vector<std::string> buttons() const;
    bool buttonEnabled(int i) const;

    bool open_ = false;
    Mode mode_ = Mode::Main;
    Result result_ = Result::None;
    std::vector<u8> snapshot_;
    Slot slots_[kSlots];
    int selected_ = -1;
    std::string edit_;          // the name being typed in Save
    float mouseX_ = -1, mouseY_ = -1;
    sf::Clock blink_, lastClick_;
    int lastClickSlot_ = -1;
    std::string note_;          // a line shown when something went wrong
};
