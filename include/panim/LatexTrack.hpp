// Timeline-driven LaTeX morphing inspired by SwapTube's LatexDemo.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Frame.hpp"
#include "LatexRenderer.hpp"
#include "Status.hpp"

namespace panim {

    class LatexTrack {
    public:
        struct Keyframe {
            std::string latex;
            double hold_sec;
            double transition_sec;
        };

        void set_center_norm(double nx, double ny) {
            center_norm_x_ = nx;
            center_norm_y_ = ny;
        }
        void set_target_height_ratio(double r) { target_height_ratio_ = r; }
        void set_tint(uint8_t r, uint8_t g, uint8_t b) {
            tint_r_ = r;
            tint_g_ = g;
            tint_b_ = b;
            tint_set_ = true;
        }

        void add_keyframe(const std::string &latex, double hold_sec, double transition_sec);

        // Pre-render SVGs to frames. frame_height is the target video height.
        Status prepare(LatexRenderer &renderer, int frame_height);

        // Render into target at timeline time t (seconds).
        void render(Frame &target, double t) const;

        double duration() const { return total_duration_; }
        bool ready() const { return ready_; }

    private:
        struct Segment {
            int from_idx;
            int to_idx;
            double start;
            double end;
            bool is_transition;
        };

        std::vector<Keyframe> keys_;
        std::vector<Frame> frames_;
        std::vector<Segment> segments_;
        double total_duration_ = 0.0;
        double center_norm_x_ = 0.5;
        double center_norm_y_ = 0.6;
        double target_height_ratio_ = 0.28;
        uint8_t tint_r_ = 255, tint_g_ = 255, tint_b_ = 255;
        bool tint_set_ = false;
        bool ready_ = false;
    };

} // namespace panim
