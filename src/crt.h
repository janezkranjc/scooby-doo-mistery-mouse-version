// Optional CRT look for the picture: scanlines, a shadow mask and a slightly
// curved screen. Off by default; F9 steps through the settings.
#pragma once
#include <SFML/Graphics.hpp>
#include <optional>
#include <string>

class CrtFilter {
public:
    enum class Mode { Off, Scanlines, Crt };

    // Compiles the shader. Returns false (and the filter stays off) when the
    // graphics driver has no shader support or rejects it.
    bool init();
    bool available() const { return shader_.has_value(); }

    Mode mode() const { return mode_; }
    void setMode(Mode m) { mode_ = available() ? m : Mode::Off; }
    void next();
    static const char* name(Mode m);
    static std::optional<Mode> parse(const std::string& s);

    // Render state to draw the game picture with. `inputWidth` is how much of
    // the texture the picture uses (256 or 320 pixels).
    sf::RenderStates states(const sf::Texture& texture, int inputWidth);

private:
    std::optional<sf::Shader> shader_;
    Mode mode_ = Mode::Off;
};
