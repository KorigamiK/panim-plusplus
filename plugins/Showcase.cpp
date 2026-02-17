#include <cmath>
#include <utility>

#include "panim/Animation.hpp"
#include "panim/CudaHelpers.hpp"
#include "panim/EquationMorph.hpp"
#include "panim/Frame.hpp"
#include "panim/LatexRenderer.hpp"
#include "panim/LatexTrack.hpp"
#include "panim/Log.hpp"
#include "panim/Painter.hpp"
#include "panim/Timeline.hpp"

using panim::Color;
using panim::Frame;
using panim::AnimationContext;
using panim::Painter;
namespace anim = panim::anim;

namespace {

    class Showcase : public panim::Animation {
    public:
        void on_setup(const AnimationContext &ctx) override {
            ctx_ = ctx;
            loop_duration_ = 10.0;

            // Background gradient over time.
            bg_top_.add(0.0, Color{18, 28, 68});
            bg_top_.add(3.0, Color{44, 16, 76});
            bg_top_.add(6.5, Color{10, 40, 62});
            bg_top_.add(loop_duration_, Color{18, 28, 68});

            bg_bottom_.add(0.0, Color{14, 48, 88});
            bg_bottom_.add(3.2, Color{22, 86, 110});
            bg_bottom_.add(6.8, Color{12, 96, 86});
            bg_bottom_.add(loop_duration_, Color{14, 48, 88});

            grid_alpha_.add(0.0, 0.18);
            grid_alpha_.add(4.0, 0.42);
            grid_alpha_.add(8.0, 0.16);
            grid_alpha_.add(loop_duration_, 0.18);

            blob1_pos_.add(0.0, {0.18, 0.42});
            blob1_pos_.add(2.6, {0.72, 0.36});
            blob1_pos_.add(5.6, {0.68, 0.78});
            blob1_pos_.add(loop_duration_, {0.18, 0.42});

            blob2_pos_.add(0.0, {0.78, 0.68});
            blob2_pos_.add(3.0, {0.32, 0.30});
            blob2_pos_.add(6.0, {0.24, 0.72});
            blob2_pos_.add(loop_duration_, {0.78, 0.68});

            blob1_r_.add(0.0, 130.0);
            blob1_r_.add(3.0, 90.0);
            blob1_r_.add(6.0, 160.0);
            blob1_r_.add(loop_duration_, 130.0);

            blob2_r_.add(0.0, 110.0);
            blob2_r_.add(3.0, 170.0);
            blob2_r_.add(6.0, 95.0);
            blob2_r_.add(loop_duration_, 110.0);

            blob1_col_.add(0.0, Color{220, 120, 255, 200});
            blob1_col_.add(3.0, Color{140, 180, 255, 200});
            blob1_col_.add(6.0, Color{255, 200, 120, 200});
            blob1_col_.add(loop_duration_, Color{220, 120, 255, 200});

            blob2_col_.add(0.0, Color{120, 240, 200, 190});
            blob2_col_.add(3.0, Color{90, 180, 255, 190});
            blob2_col_.add(6.0, Color{255, 160, 180, 190});
            blob2_col_.add(loop_duration_, Color{120, 240, 200, 190});

            // Optional LaTeX elements.
            latex_ready_ = ctx.latex != nullptr;
            if (latex_ready_) {
                auto st = morph_.init("e^{i\\pi}+1=0", "\\nabla \\times (\\vec E)= -\\partial_t \\vec B", *ctx.latex, 1.0, static_cast<int>(ctx.height * 0.22));
                if (!st.ok) {
                    PANIM_LOG_WARN("Showcase: morph init failed: {}", st.message);
                    latex_ready_ = false;
                } else {
                    morph_.set_center_norm(0.5, 0.6);
                }

                caption_.set_center_norm(0.5, 0.14);
                caption_.set_target_height_ratio(0.10);
                caption_.add_keyframe("\\text{Hardware accel when available}", 1.0, 0.6);
                caption_.add_keyframe("\\text{Timeline-driven blobs}", 1.0, 0.6);
                caption_.add_keyframe("\\text{LaTeX overlays (optional)}", 1.0, 0.6);
                caption_.add_keyframe("\\text{Replace me with your scene}", 1.0, 0.6);
                auto st2 = caption_.prepare(*ctx.latex, ctx.height);
                if (!st2.ok) {
                    PANIM_LOG_WARN("Showcase: caption prep failed: {}", st2.message);
                    latex_ready_ = false;
                }
            }

            badge_ = Frame(128, 128);
            Painter p(badge_);
            p.fill_vertical_gradient(Color{255, 220, 120}, Color{255, 120, 80});
            p.fill_circle(64, 64, 46, Color{40, 20, 10, 200});
        }

        void render_frame(Frame &frame, double t) override {
            double tt = std::fmod(t, loop_duration_);
            if (tt < 0)
                tt += loop_duration_;

            Painter paint(frame);
            paint.fill_vertical_gradient(bg_top_.sample_loop(tt), bg_bottom_.sample_loop(tt));

            draw_grid(frame, grid_alpha_.sample_loop(tt));
            draw_blobs(frame, tt);
            draw_badge(frame, tt);

            if (latex_ready_ && morph_.ready()) {
                double phase = 0.5 + 0.5 * std::sin(tt * 0.8);
                morph_.render(frame, phase);
            }
            if (latex_ready_ && caption_.ready()) {
                double ct = std::fmod(tt, caption_.duration());
                caption_.render(frame, ct);
            }
        }

    private:
        AnimationContext ctx_{};
        double loop_duration_ = 10.0;

        anim::Track<Color> bg_top_;
        anim::Track<Color> bg_bottom_;
        anim::Track<double> grid_alpha_;
        anim::Track<panim::Vec2> blob1_pos_;
        anim::Track<panim::Vec2> blob2_pos_;
        anim::Track<double> blob1_r_;
        anim::Track<double> blob2_r_;
        anim::Track<Color> blob1_col_;
        anim::Track<Color> blob2_col_;

        panim::EquationMorph morph_;
        panim::LatexTrack caption_;
        bool latex_ready_ = false;

        Frame badge_{0, 0};
        bool cuda_logged_ = false;

        void draw_grid(Frame &frame, double alpha) {
            if (alpha <= 0.0)
                return;
            Painter p(frame);
            int step = std::max(32, ctx_.width / 18);
            for (int x = 0; x < ctx_.width; x += step) {
                p.stroke_line(x, 0, x, ctx_.height, 1, Color{255, 255, 255}, static_cast<float>(alpha));
            }
            for (int y = 0; y < ctx_.height; y += step) {
                p.stroke_line(0, y, ctx_.width, y, 1, Color{255, 255, 255}, static_cast<float>(alpha * 0.9));
            }
        }

        void draw_blobs(Frame &frame, double t) {
            Painter p(frame);

            auto pos1 = blob1_pos_.sample_loop(t);
            auto pos2 = blob2_pos_.sample_loop(t);
            int x1 = static_cast<int>(pos1.x * ctx_.width);
            int y1 = static_cast<int>(pos1.y * ctx_.height);
            int x2 = static_cast<int>(pos2.x * ctx_.width);
            int y2 = static_cast<int>(pos2.y * ctx_.height);

            int r1 = static_cast<int>(blob1_r_.sample_loop(t));
            int r2 = static_cast<int>(blob2_r_.sample_loop(t));
            Color c1 = blob1_col_.sample_loop(t);
            Color c2 = blob2_col_.sample_loop(t);

            p.fill_circle(x1, y1, r1, c1, 0.65f);
            p.fill_circle(x2, y2, r2, c2, 0.65f);

            // Soft link line.
            p.stroke_line(x1, y1, x2, y2, 5, Color{255, 255, 255, 220}, 0.18f);
            p.stroke_line(x1, y1, x2, y2, 1, Color{255, 255, 255, 255}, 0.35f);
        }

        void draw_badge(Frame &frame, double t) {
            // Animate badge opacity and spin hue using CUDA invert on the small badge when available.
            float opacity = static_cast<float>(0.55 + 0.25 * std::sin(t * 1.3));
            Frame temp = badge_;
            bool gpu_ok = gpu_invert(temp);
            if (gpu_ok && !cuda_logged_) {
                PANIM_LOG_INFO("Showcase: CUDA invert active on badge overlay");
                cuda_logged_ = true;
            }
            Painter p(frame);
            int px = static_cast<int>(ctx_.width * 0.08);
            int py = static_cast<int>(ctx_.height * 0.14);
            p.blit(temp, px, py, opacity);
        }
    };

} // namespace

extern "C" panim::Animation *create_animation() { return new Showcase(); }
extern "C" void destroy_animation(panim::Animation *a) { delete a; }
