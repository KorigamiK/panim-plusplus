#include <algorithm>
#include <cmath>
#include <string>

#include "panim/Animation.hpp"
#include "panim/Compute.hpp"
#include "panim/Frame.hpp"
#include "panim/LatexTrack.hpp"
#include "panim/Log.hpp"
#include "panim/Painter.hpp"
#include "panim/Plugin.hpp"
#include "panim/SceneSequence.hpp"
#include "panim/Timeline.hpp"

namespace {

    std::string latex_text(std::string text) {
        for (char &character : text) {
            switch (character) {
            case '\\':
            case '{':
            case '}':
            case '$':
            case '&':
            case '#':
            case '%':
            case '_':
            case '^':
                character = ' ';
                break;
            default:
                break;
            }
        }
        return "\\text{" + text + "}";
    }

    class FeatureTour : public panim::Animation {
    public:
        panim::AnimationInfo info() const override { return {"FeatureTour", 15.0, 1920, 1080, 30.0}; }

        void on_setup(const panim::AnimationContext &ctx) override {
            ctx_ = ctx;
            device_ = panim::compute_device();

            int fractal_divisor = device_.hardware_accelerated ? 1 : 4;
            fractal_ = panim::Frame(std::max(1, ctx.width / fractal_divisor), std::max(1, ctx.height / fractal_divisor));

            prepare_text();
            prepare_motion();

            scenes_.add("hello", 2.6, [this](panim::Frame &frame, const panim::SceneTime &time) { render_intro(frame, time); }, 0.0);
            scenes_.add("mandelbulb", 4.0, [this](panim::Frame &frame, const panim::SceneTime &time) { render_mandelbulb(frame, time); });
            scenes_.add("integral", 5.2, [this](panim::Frame &frame, const panim::SceneTime &time) { render_integral(frame, time); });
            scenes_.add("compose", 3.2, [this](panim::Frame &frame, const panim::SceneTime &time) { render_outro(frame, time); });
        }

        void render_frame(panim::Frame &frame, double time_seconds) override {
            scenes_.render(frame, time_seconds);
            draw_progress(frame, time_seconds);
        }

    private:
        panim::AnimationContext ctx_{};
        panim::ComputeDeviceInfo device_{};
        panim::SceneSequence scenes_;
        panim::Frame fractal_{0, 0};
        panim::LatexTrack intro_text_;
        panim::LatexTrack mandelbulb_text_;
        panim::LatexTrack integral_text_;
        panim::LatexTrack integral_caption_;
        panim::LatexTrack outro_text_;
        panim::anim::Track<panim::Vec2> motion_;
        panim::anim::Track<panim::Color> accent_;
        bool compute_logged_ = false;

        void prepare_track(panim::LatexTrack &track, const char *name) {
            if (!ctx_.latex)
                return;
            panim::Status status = track.prepare(*ctx_.latex, ctx_.height);
            if (!status.ok) {
                PANIM_LOG_WARN("FeatureTour {} text unavailable: {}", name, status.message);
            }
        }

        void prepare_text() {
            intro_text_.set_center_norm(0.5, 0.47);
            intro_text_.set_target_height_ratio(0.11);
            intro_text_.add_keyframe("\\text{panim++}", 0.75, 0.0, 1.18);
            intro_text_.add_keyframe("\\text{panim++ / code becomes motion}", 1.15, 0.7, 0.88);

            mandelbulb_text_.set_center_norm(0.5, 0.10);
            mandelbulb_text_.set_target_height_ratio(0.047);
            mandelbulb_text_.add_keyframe(latex_text("Mandelbulb / auto-selected " + device_.device_name), 30.0, 0.0);

            integral_text_.set_center_norm(0.5, 0.54);
            integral_text_.set_target_height_ratio(0.135);
            integral_text_.add_keyframe("I=\\int_{-\\infty}^{\\infty}e^{-x^2}\\,dx", 0.55, 0.0);
            integral_text_.add_keyframe("I^2=\\int_{-\\infty}^{\\infty}"
                                        "\\int_{-\\infty}^{\\infty}e^{-(x^2+y^2)}\\,dx\\,dy",
                                        0.42, 0.62);
            integral_text_.add_keyframe("I^2=\\int_0^{2\\pi}\\int_0^{\\infty}"
                                        "e^{-r^2}r\\,dr\\,d\\theta",
                                        0.42, 0.62);
            integral_text_.add_keyframe("I^2=\\pi", 0.42, 0.62);
            integral_text_.add_keyframe("I=\\sqrt{\\pi}", 0.7, 0.62);

            integral_caption_.set_center_norm(0.5, 0.14);
            integral_caption_.set_target_height_ratio(0.047);
            integral_caption_.add_keyframe("\\text{glyph-aware equation morphing}", 30.0, 0.0);

            outro_text_.set_center_norm(0.5, 0.47);
            outro_text_.set_target_height_ratio(0.058);
            outro_text_.add_keyframe("\\text{Painter + Timeline + Scenes / one small C++ plugin}", 30.0, 0.0);

            prepare_track(intro_text_, "intro");
            prepare_track(mandelbulb_text_, "Mandelbulb");
            prepare_track(integral_text_, "integral");
            prepare_track(integral_caption_, "integral caption");
            prepare_track(outro_text_, "outro");
        }

        void prepare_motion() {
            motion_.add(0.0, {0.18, 0.66});
            motion_.add(0.9, {0.48, 0.66}, panim::anim::ease_out_back);
            motion_.add(1.8, {0.78, 0.66});
            motion_.add(3.2, {0.18, 0.66});

            accent_.add(0.0, {92, 214, 255, 255});
            accent_.add(1.1, {239, 112, 194, 255});
            accent_.add(2.2, {255, 201, 98, 255});
            accent_.add(3.2, {92, 214, 255, 255});
        }

        void draw_ambient(panim::Frame &frame, panim::Color top, panim::Color bottom, double time_seconds) {
            panim::Painter painter(frame);
            painter.fill_vertical_gradient(top, bottom);
            int spacing = std::max(36, ctx_.width / 18);
            int line_width = std::max(1, ctx_.height / 720);
            int drift = static_cast<int>(std::fmod(time_seconds * 18.0, spacing));
            for (int x = -spacing; x < ctx_.width + spacing; x += spacing) {
                painter.stroke_line(x + drift, 0, x + drift - ctx_.height / 3, ctx_.height, line_width, {150, 190, 255, 255}, 0.08f);
            }
        }

        void render_intro(panim::Frame &frame, const panim::SceneTime &time) {
            draw_ambient(frame, {9, 16, 38, 255}, {30, 16, 62, 255}, time.local_seconds);
            panim::Painter painter(frame);
            int pulse = static_cast<int>(ctx_.height * (0.12 + 0.018 * std::sin(time.local_seconds * 2.4)));
            painter.fill_circle(ctx_.width / 2, static_cast<int>(ctx_.height * 0.48), pulse * 2, {74, 101, 255, 255}, 0.09f);
            if (intro_text_.ready())
                intro_text_.render(frame, time.local_seconds);
        }

        void render_mandelbulb(panim::Frame &frame, const panim::SceneTime &time) {
            fractal_.clear(0, 0, 0, 255);
            panim::ComputeParams params;
            params.time_seconds = static_cast<float>(time.local_seconds + 1.4);
            panim::ComputeResult result = panim::apply_compute_effect(fractal_, panim::ComputeEffect::Mandelbulb, params);
            if (!compute_logged_) {
                if (result.ok) {
                    PANIM_LOG_INFO("FeatureTour Mandelbulb running on {} ({})", device_.backend_name, device_.device_name);
                } else {
                    PANIM_LOG_ERROR("FeatureTour Mandelbulb failed: {}", result.message);
                }
                compute_logged_ = true;
            }

            panim::Painter painter(frame);
            painter.blit_scaled(fractal_, 0, 0, ctx_.width, ctx_.height);
            painter.fill_rect(0, 0, ctx_.width, static_cast<int>(ctx_.height * 0.18), {4, 7, 20, 255}, 0.58f);
            if (mandelbulb_text_.ready())
                mandelbulb_text_.render(frame, time.local_seconds);
        }

        void render_integral(panim::Frame &frame, const panim::SceneTime &time) {
            draw_ambient(frame, {7, 25, 38, 255}, {12, 57, 68, 255}, time.local_seconds + 4.0);
            panim::Painter painter(frame);
            int graph_left = static_cast<int>(ctx_.width * 0.14);
            int graph_right = static_cast<int>(ctx_.width * 0.86);
            int baseline = static_cast<int>(ctx_.height * 0.80);
            int graph_height = static_cast<int>(ctx_.height * 0.14);
            int baseline_width = std::max(2, ctx_.height / 360);
            int curve_width = std::max(3, ctx_.height / 240);
            painter.stroke_line(graph_left, baseline, graph_right, baseline, baseline_width, {124, 220, 212, 255}, 0.36f);
            int previous_x = graph_left;
            int previous_y = baseline;
            for (int x = graph_left + 1; x <= graph_right; ++x) {
                double nx = (x - graph_left) / static_cast<double>(graph_right - graph_left) * 6.0 - 3.0;
                int y = baseline - static_cast<int>(std::exp(-nx * nx) * graph_height);
                painter.stroke_line(previous_x, previous_y, x, y, curve_width, {124, 240, 224, 255}, 0.72f);
                previous_x = x;
                previous_y = y;
            }
            if (integral_caption_.ready())
                integral_caption_.render(frame, time.local_seconds);
            if (integral_text_.ready()) {
                integral_text_.render(frame, std::min(time.local_seconds, integral_text_.duration()));
            }
        }

        void render_outro(panim::Frame &frame, const panim::SceneTime &time) {
            draw_ambient(frame, {20, 13, 47, 255}, {10, 31, 58, 255}, time.local_seconds + 8.0);
            panim::Vec2 position = motion_.sample(time.local_seconds);
            panim::Color accent = accent_.sample(time.local_seconds);
            panim::Painter painter(frame);
            int radius = std::max(22, static_cast<int>(ctx_.height * 0.055));
            int x = static_cast<int>(position.x * ctx_.width);
            int y = static_cast<int>(position.y * ctx_.height);
            int line_width = std::max(3, ctx_.height / 240);
            painter.stroke_line(static_cast<int>(ctx_.width * 0.18), static_cast<int>(ctx_.height * 0.66), static_cast<int>(ctx_.width * 0.78),
                                static_cast<int>(ctx_.height * 0.66), line_width, accent, 0.24f);
            painter.fill_circle(x, y, radius + 18, accent, 0.12f);
            painter.fill_circle(x, y, radius, accent, 0.82f);
            if (outro_text_.ready())
                outro_text_.render(frame, time.local_seconds);
        }

        void draw_progress(panim::Frame &frame, double time_seconds) {
            double progress = scenes_.duration() > 0.0 ? std::clamp(time_seconds / scenes_.duration(), 0.0, 1.0) : 0.0;
            panim::Painter painter(frame);
            int margin = std::max(16, ctx_.width / 40);
            int width = ctx_.width - margin * 2;
            int y = ctx_.height - std::max(12, ctx_.height / 45);
            int bar_height = std::max(3, ctx_.height / 240);
            painter.fill_rect(margin, y, width, bar_height, {255, 255, 255, 255}, 0.14f);
            painter.fill_rect(margin, y, static_cast<int>(width * progress), bar_height, {132, 224, 255, 255}, 0.75f);
        }
    };

} // namespace

PANIM_EXPORT_ANIMATION(FeatureTour)
