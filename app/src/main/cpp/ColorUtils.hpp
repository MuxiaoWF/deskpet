#pragma once

#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ColorUtils {

inline void RgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float maxC = std::max({r, g, b});
    float minC = std::min({r, g, b});
    l = (maxC + minC) * 0.5f;
    if (maxC == minC) { h = s = 0.0f; return; }
    float d = maxC - minC;
    s = (l > 0.5f) ? d / (2.0f - maxC - minC) : d / (maxC + minC);
    if (maxC == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (maxC == g) h = (b - r) / d + 2.0f;
    else h = (r - g) / d + 4.0f;
    h /= 6.0f;
}

inline float HueToRgb(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

inline void HslToRgb(float h, float s, float l, float& r, float& g, float& b) {
    if (s == 0.0f) { r = g = b = l; return; }
    float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    r = HueToRgb(p, q, h + 1.0f / 3.0f);
    g = HueToRgb(p, q, h);
    b = HueToRgb(p, q, h - 1.0f / 3.0f);
}

inline bool ParseHexColor(const std::string& hex, float& outR, float& outG, float& outB, float& outA) {
    std::string h = hex;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    if (h.size() == 6) h += "FF";
    if (h.size() != 8) return false;
    unsigned int packed = 0;
    try { packed = std::stoul(h, nullptr, 16); } catch (...) { return false; }
    outR = ((packed >> 24) & 0xFF) / 255.0f;
    outG = ((packed >> 16) & 0xFF) / 255.0f;
    outB = ((packed >> 8)  & 0xFF) / 255.0f;
    outA = (packed & 0xFF) / 255.0f;
    return true;
}

} // namespace ColorUtils
