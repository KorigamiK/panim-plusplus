#pragma once

#include <algorithm>
#include <cstdint>

namespace panim {

    struct Color {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        uint8_t a = 255;
    };

    inline Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return Color{r, g, b, a}; }

    inline Color lerp_color(const Color &a, const Color &b, double t) {
        double u = std::clamp(t, 0.0, 1.0);
        auto lerp_chan = [u](uint8_t ca, uint8_t cb) { return static_cast<uint8_t>(ca + (cb - ca) * u); };
        return Color{lerp_chan(a.r, b.r), lerp_chan(a.g, b.g), lerp_chan(a.b, b.b), lerp_chan(a.a, b.a)};
    }

} // namespace panim
