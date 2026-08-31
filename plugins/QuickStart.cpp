#include <algorithm>

#include "panim/Animation.hpp"
#include "panim/EquationMorph.hpp"
#include "panim/Painter.hpp"
#include "panim/Plugin.hpp"

namespace {

    constexpr double scene_duration = 4.0;
    constexpr double demo_duration = scene_duration * 2.0;

    double smoothstep(double value) {
        double t = std::clamp(value, 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    void fill_rounded_rect(panim::Painter &painter, int cx, int cy, int half_width, int half_height, int radius, panim::Color color) {
        radius = std::clamp(radius, 0, std::min(half_width, half_height));
        painter.fill_rect(cx - half_width + radius, cy - half_height, 2 * (half_width - radius), 2 * half_height, color);
        painter.fill_rect(cx - half_width, cy - half_height + radius, 2 * half_width, 2 * (half_height - radius), color);
        if (radius == 0)
            return;

        painter.fill_circle(cx - half_width + radius, cy - half_height + radius, radius, color);
        painter.fill_circle(cx + half_width - radius, cy - half_height + radius, radius, color);
        painter.fill_circle(cx - half_width + radius, cy + half_height - radius, radius, color);
        painter.fill_circle(cx + half_width - radius, cy + half_height - radius, radius, color);
    }

    class QuickStart final : public panim::Animation {
    public:
        panim::AnimationInfo info() const override { return {"QuickStart", demo_duration, 1280, 720, 30.0}; }

        void on_setup(const panim::AnimationContext &context) override {
            context_ = context;
            if (context.latex) {
                equation_.init("x^2 + 2x + 1", "(x + 1)^2", *context.latex, 1.0, static_cast<int>(context.height * 0.18));
                equation_.set_center_norm(0.5, 0.5);
                eq2_.init("\\text{Hello World}!", "\\text{Hi there}!", *context.latex, 1.0, static_cast<int>(context.height * 0.18));
                eq2_.set_center_norm(0.5, 0.4);
            }
        }

        void render_frame(panim::Frame &frame, double time_seconds) override {
            panim::Painter painter(frame);
            painter.fill_vertical_gradient({7, 14, 31, 255}, {22, 43, 68, 255});

            if (time_seconds < scene_duration) {
                int start_x = context_.width / 6;
                int end_x = context_.width - start_x;
                int y = context_.height / 2;
                double progress = smoothstep(time_seconds / scene_duration);
                int x = start_x + static_cast<int>((end_x - start_x) * progress);
                int half_width = 28 + static_cast<int>((90 - 28) * progress);
                int half_height = 28 + static_cast<int>((52 - 28) * progress);
                int corner_radius = static_cast<int>(28 * (1.0 - progress));
                if (eq2_.ready())
                    eq2_.render(frame, smoothstep(time_seconds / (scene_duration * 0.5)));
                painter.stroke_line(start_x, y, end_x, y, 4, {105, 214, 255, 255}, 0.25f);
                fill_rounded_rect(painter, x, y, half_width, half_height, corner_radius, {105, 214, 255, 255});
                return;
            }

            if (equation_.ready()) {
                double scene_time = time_seconds - scene_duration;
                double morph_progress = smoothstep((scene_time - 1.0) / 2.0);
                equation_.render(frame, morph_progress);
            }
        }

    private:
        panim::AnimationContext context_{};
        panim::EquationMorph equation_;
        panim::EquationMorph eq2_;
    };

} // namespace

PANIM_EXPORT_ANIMATION(QuickStart)
