#include "crt.h"

namespace {

// Timothy Lottes' CRT shader, which he released into the public domain
// ("Please take and use, change, or whatever."), adapted for SFML: GLSL 1.20,
// SFML's texture coordinates, and the settings passed in as uniforms. The
// copy it was taken from is libretro's glsl-shaders/crt/shaders/crt-lottes.glsl.
const char* kFragment = R"(
#version 120
uniform sampler2D source;
uniform vec2 textureSize;   // whole texture, in pixels
uniform vec2 inputSize;     // the part holding the picture
uniform float hardScan, hardPix, warpX, warpY, maskDark, maskLight, shadowMask;
uniform float brightBoost, hardBloomPix, hardBloomScan, bloomAmount, shape;

float toLinear1(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
vec3 toLinear(vec3 c) { return vec3(toLinear1(c.r), toLinear1(c.g), toLinear1(c.b)); }
float toSrgb1(float c) { return c < 0.0031308 ? c * 12.92 : 1.055 * pow(c, 0.41666) - 0.055; }
vec3 toSrgb(vec3 c) { return vec3(toSrgb1(c.r), toSrgb1(c.g), toSrgb1(c.b)); }

// Nearest source pixel to a position plus a pixel offset, black outside the picture.
vec3 fetch(vec2 pos, vec2 off) {
    vec2 p = floor(pos * textureSize + off) + 0.5;
    if (p.x < 0.0 || p.y < 0.0 || p.x > inputSize.x || p.y > inputSize.y) return vec3(0.0);
    return toLinear(brightBoost * texture2D(source, p / textureSize).rgb);
}
vec2 dist(vec2 pos) { pos *= textureSize; return -((pos - floor(pos)) - 0.5); }
float gaus(float pos, float scale) { return exp2(scale * pow(abs(pos), shape)); }

vec3 horz3(vec2 pos, float off) {
    vec3 b = fetch(pos, vec2(-1.0, off)), c = fetch(pos, vec2(0.0, off)), d = fetch(pos, vec2(1.0, off));
    float dst = dist(pos).x;
    float wb = gaus(dst - 1.0, hardPix), wc = gaus(dst, hardPix), wd = gaus(dst + 1.0, hardPix);
    return (b * wb + c * wc + d * wd) / (wb + wc + wd);
}
vec3 horz5(vec2 pos, float off) {
    vec3 a = fetch(pos, vec2(-2.0, off)), b = fetch(pos, vec2(-1.0, off)), c = fetch(pos, vec2(0.0, off));
    vec3 d = fetch(pos, vec2(1.0, off)), e = fetch(pos, vec2(2.0, off));
    float dst = dist(pos).x;
    float wa = gaus(dst - 2.0, hardPix), wb = gaus(dst - 1.0, hardPix), wc = gaus(dst, hardPix);
    float wd = gaus(dst + 1.0, hardPix), we = gaus(dst + 2.0, hardPix);
    return (a * wa + b * wb + c * wc + d * wd + e * we) / (wa + wb + wc + wd + we);
}
vec3 horz7(vec2 pos, float off) {
    vec3 a = fetch(pos, vec2(-3.0, off)), b = fetch(pos, vec2(-2.0, off)), c = fetch(pos, vec2(-1.0, off));
    vec3 d = fetch(pos, vec2(0.0, off)), e = fetch(pos, vec2(1.0, off)), f = fetch(pos, vec2(2.0, off));
    vec3 g = fetch(pos, vec2(3.0, off));
    float dst = dist(pos).x;
    float wa = gaus(dst - 3.0, hardBloomPix), wb = gaus(dst - 2.0, hardBloomPix), wc = gaus(dst - 1.0, hardBloomPix);
    float wd = gaus(dst, hardBloomPix), we = gaus(dst + 1.0, hardBloomPix), wf = gaus(dst + 2.0, hardBloomPix);
    float wg = gaus(dst + 3.0, hardBloomPix);
    return (a * wa + b * wb + c * wc + d * wd + e * we + f * wf + g * wg) / (wa + wb + wc + wd + we + wf + wg);
}
float scan(vec2 pos, float off) { return gaus(dist(pos).y + off, hardScan); }
float bloomScan(vec2 pos, float off) { return gaus(dist(pos).y + off, hardBloomScan); }

vec3 tri(vec2 pos) {
    return horz3(pos, -1.0) * scan(pos, -1.0) + horz5(pos, 0.0) * scan(pos, 0.0) + horz3(pos, 1.0) * scan(pos, 1.0);
}
vec3 bloom(vec2 pos) {
    return horz5(pos, -2.0) * bloomScan(pos, -2.0) + horz7(pos, -1.0) * bloomScan(pos, -1.0) +
           horz7(pos, 0.0) * bloomScan(pos, 0.0) + horz7(pos, 1.0) * bloomScan(pos, 1.0) +
           horz5(pos, 2.0) * bloomScan(pos, 2.0);
}

// The curve of the glass, on 0..1 picture coordinates.
vec2 warp(vec2 pos) {
    pos = pos * 2.0 - 1.0;
    pos *= vec2(1.0 + pos.y * pos.y * warpX, 1.0 + pos.x * pos.x * warpY);
    return pos * 0.5 + 0.5;
}

// Phosphor pattern, laid over window pixels.
vec3 mask(vec2 pos) {
    vec3 m = vec3(maskDark);
    if (shadowMask == 1.0) {
        float line = maskLight, odd = 0.0;
        if (fract(pos.x * 0.166666666) < 0.5) odd = 1.0;
        if (fract((pos.y + odd) * 0.5) < 0.5) line = maskDark;
        pos.x = fract(pos.x * 0.333333333);
        if (pos.x < 0.333) m.r = maskLight; else if (pos.x < 0.666) m.g = maskLight; else m.b = maskLight;
        m *= line;
    } else if (shadowMask == 2.0) {
        pos.x = fract(pos.x * 0.333333333);
        if (pos.x < 0.333) m.r = maskLight; else if (pos.x < 0.666) m.g = maskLight; else m.b = maskLight;
    } else if (shadowMask == 3.0) {
        pos.x += pos.y * 3.0;
        pos.x = fract(pos.x * 0.166666666);
        if (pos.x < 0.333) m.r = maskLight; else if (pos.x < 0.666) m.g = maskLight; else m.b = maskLight;
    }
    return m;
}

void main() {
    // Position on the picture from 0 to 1, from the sprite's texture
    // coordinates (window pixels can differ from them on high-DPI screens).
    vec2 uv = warp(gl_TexCoord[0].xy * textureSize / inputSize);
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) { gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
    vec2 pos = uv * inputSize / textureSize;
    vec3 colour = tri(pos) + bloom(pos) * bloomAmount;
    if (shadowMask > 0.0) colour *= mask(gl_FragCoord.xy * 1.000001);
    gl_FragColor = vec4(toSrgb(colour), 1.0);
}
)";

struct Settings {
    float hardScan, hardPix, warpX, warpY, maskDark, maskLight, shadowMask, brightBoost, bloomAmount;
};

// "Scanlines": flat screen, gentle lines, no phosphor mask. "CRT": Lottes'
// own defaults with the aperture grille, a little brighter to make up for it.
const Settings kScanlines{-6.0f, -3.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.10f};
const Settings kCrt{-8.0f, -3.0f, 0.031f, 0.041f, 0.5f, 1.5f, 2.0f, 1.25f, 0.15f};

}  // namespace

bool CrtFilter::init() {
    if (!sf::Shader::isAvailable()) return false;
    sf::Shader s;
    if (!s.loadFromMemory(std::string(kFragment), sf::Shader::Type::Fragment)) return false;
    shader_ = std::move(s);
    return true;
}

void CrtFilter::next() {
    if (!available()) return;
    mode_ = mode_ == Mode::Off ? Mode::Scanlines : mode_ == Mode::Scanlines ? Mode::Crt : Mode::Off;
}

const char* CrtFilter::name(Mode m) {
    return m == Mode::Scanlines ? "scanlines" : m == Mode::Crt ? "crt" : "off";
}

std::optional<CrtFilter::Mode> CrtFilter::parse(const std::string& s) {
    if (s == "off") return Mode::Off;
    if (s == "scanlines") return Mode::Scanlines;
    if (s == "crt") return Mode::Crt;
    return std::nullopt;
}

sf::RenderStates CrtFilter::states(const sf::Texture& texture, int inputWidth) {
    sf::RenderStates rs;
    if (mode_ == Mode::Off || !shader_) return rs;
    const Settings& k = mode_ == Mode::Crt ? kCrt : kScanlines;
    sf::Shader& s = *shader_;
    s.setUniform("source", texture);
    s.setUniform("textureSize", sf::Glsl::Vec2(sf::Vector2f(texture.getSize())));
    s.setUniform("inputSize", sf::Glsl::Vec2(float(inputWidth), float(texture.getSize().y)));
    s.setUniform("hardScan", k.hardScan);
    s.setUniform("hardPix", k.hardPix);
    s.setUniform("warpX", k.warpX);
    s.setUniform("warpY", k.warpY);
    s.setUniform("maskDark", k.maskDark);
    s.setUniform("maskLight", k.maskLight);
    s.setUniform("shadowMask", k.shadowMask);
    s.setUniform("brightBoost", k.brightBoost);
    s.setUniform("hardBloomPix", -1.5f);
    s.setUniform("hardBloomScan", -2.0f);
    s.setUniform("bloomAmount", k.bloomAmount);
    s.setUniform("shape", 2.0f);
    rs.shader = &s;
    return rs;
}
