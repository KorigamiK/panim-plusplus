#pragma once

#include <algorithm>

namespace panim {

    struct Vec2 {
        double x = 0.0;
        double y = 0.0;
    };

    template <typename T>
    inline T clamp(const T &v, const T &lo, const T &hi) {
        return std::min(std::max(v, lo), hi);
    }

    inline Vec2 lerp_vec2(const Vec2 &a, const Vec2 &b, double t) {
        double u = clamp(t, 0.0, 1.0);
        return Vec2{a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u};
    }

} // namespace panim

