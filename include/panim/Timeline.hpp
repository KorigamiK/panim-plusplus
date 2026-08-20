#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <vector>

#include "panim/Color.hpp"
#include "panim/Math.hpp"

namespace panim::anim {

    using EaseFn = double (*)(double);

    inline double ease_linear(double t) { return t; }

    inline double ease_smootherstep(double t) {
        // Quintic smoothstep: 6t^5 - 15t^4 + 10t^3
        double u = std::clamp(t, 0.0, 1.0);
        return u * u * u * (u * (u * 6.0 - 15.0) + 10.0);
    }

    inline double ease_in_out_quad(double t) {
        double u = std::clamp(t, 0.0, 1.0);
        return u < 0.5 ? 2.0 * u * u : -1.0 + (4.0 - 2.0 * u) * u;
    }

    inline double ease_out_back(double t) {
        double u = std::clamp(t, 0.0, 1.0);
        const double c1 = 1.70158;
        const double c3 = c1 + 1.0;
        return 1.0 + c3 * std::pow(u - 1.0, 3) + c1 * std::pow(u - 1.0, 2);
    }

    template <typename T> struct LerpHelper {
        static T apply(const T &, const T &, double) {
            static_assert(!sizeof(T), "LerpHelper specialization missing for type T");
            return T{};
        }
    };

    template <typename T> struct LerpHelperArithmetic {
        static T apply(const T &a, const T &b, double t) {
            double u = std::clamp(t, 0.0, 1.0);
            return static_cast<T>(a + (b - a) * u);
        }
    };

    template <> struct LerpHelper<double> : LerpHelperArithmetic<double> {};
    template <> struct LerpHelper<float> : LerpHelperArithmetic<float> {};
    template <> struct LerpHelper<int> : LerpHelperArithmetic<int> {};

    template <> struct LerpHelper<panim::Vec2> {
        static panim::Vec2 apply(const panim::Vec2 &a, const panim::Vec2 &b, double t) { return panim::lerp_vec2(a, b, t); }
    };

    template <> struct LerpHelper<panim::Color> {
        static panim::Color apply(const panim::Color &a, const panim::Color &b, double t) { return panim::lerp_color(a, b, t); }
    };

    template <typename T> inline T lerp_value(const T &a, const T &b, double t) { return LerpHelper<T>::apply(a, b, t); }

    template <typename T> struct Keyframe {
        double time = 0.0;
        T value{};
        EaseFn ease = ease_smootherstep;
    };

    template <typename T> class Track {
    public:
        void add(double time, const T &value, EaseFn ease = ease_smootherstep) {
            keys_.push_back({time, value, ease});
            std::sort(keys_.begin(), keys_.end(), [](const Keyframe<T> &a, const Keyframe<T> &b) { return a.time < b.time; });
        }

        bool empty() const { return keys_.empty(); }

        double duration() const { return keys_.empty() ? 0.0 : keys_.back().time; }

        // Sample without looping; clamps outside range.
        T sample(double t) const {
            if (keys_.empty()) {
                return T{};
            }
            if (keys_.size() == 1) {
                return keys_.front().value;
            }

            if (t <= keys_.front().time) {
                return keys_.front().value;
            }
            if (t >= keys_.back().time) {
                return keys_.back().value;
            }

            auto it = std::upper_bound(keys_.begin(), keys_.end(), t, [](double needle, const Keyframe<T> &k) { return needle < k.time; });
            const Keyframe<T> &b = *it;
            const Keyframe<T> &a = *(it - 1);
            double u = (t - a.time) / (b.time - a.time);
            double eased = b.ease ? b.ease(u) : u;
            return lerp_value(a.value, b.value, eased);
        }

        // Looping sample; if no keys return default T{}.
        T sample_loop(double t) const {
            if (keys_.empty())
                return T{};
            double dur = duration();
            if (dur <= 0.0) {
                return keys_.front().value;
            }
            double wrapped = std::fmod(t, dur);
            if (wrapped < 0)
                wrapped += dur;
            return sample(wrapped);
        }

    private:
        std::vector<Keyframe<T>> keys_;
    };

} // namespace panim::anim
