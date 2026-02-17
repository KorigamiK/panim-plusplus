#include "panim/Animation.hpp"
#include "panim/Frame.hpp"
#include "panim/LatexRenderer.hpp"
#include "panim/LatexTrack.hpp"
#include "panim/Log.hpp"
#include <cmath>

using namespace panim;

namespace {

    class LatexDemo : public Animation {
    public:
        void on_setup(const AnimationContext &ctx) override {
            ctx_ = ctx;
            if (!ctx.latex) {
                PANIM_LOG_ERROR("LatexDemo: LatexRenderer not available");
                return;
            }

            main_.set_center_norm(0.5, 0.6);
            main_.set_target_height_ratio(0.32);

            // Main timeline (tables + algebra).
            main_.add_keyframe("\\begin{tabular}{|c|c|} \\hline \\textbf{Depth} & \\textbf{Nodes} \\\\ \\hline \\end{tabular}", 1.0, 0.6);
            main_.add_keyframe("\\begin{tabular}{|c|c|} \\hline \\textbf{Depth} & \\textbf{Nodes} \\\\ \\hline 0 & 1 \\\\ \\hline \\end{tabular}", 0.8, 0.6);
            main_.add_keyframe("\\begin{tabular}{|c|c|} \\hline \\textbf{Depth} & \\textbf{Nodes} \\\\ \\hline 0 & 1 \\\\ \\hline 1 & 7 \\\\ \\hline \\end{tabular}", 0.8, 0.6);
            main_.add_keyframe("\\frac{1}{x}=\\frac{x}{y}=\\frac{y}{2}", 1.0, 0.6);
            main_.add_keyframe("1 \\cdot 2 = x \\cdot x", 0.8, 0.6);
            main_.add_keyframe("2 = x^2", 0.8, 0.6);
            main_.add_keyframe("x = \\sqrt{2}", 1.0, 0.6);
            main_.add_keyframe("\\frac{1}{x}=\\frac{x}{y} \\qquad \\frac{x}{y}=\\frac{y}{2}", 1.0, 0.6);
            main_.add_keyframe("y = x^2 \\qquad \\frac{x}{y} = \\frac{y}{2}", 1.0, 0.6);
            main_.add_keyframe("y = x^2 \\qquad 2x = y^2", 1.0, 0.6);
            main_.add_keyframe("x = \\sqrt[3]{2}", 1.0, 0.6);

            // Secondary overlay for captions; tinted blue.
            side_.set_center_norm(0.5, 0.18);
            side_.set_target_height_ratio(0.12);
            side_.set_tint(90, 150, 255);
            side_.add_keyframe("\\text{Tree growth table}", 1.2, 0.4);
            side_.add_keyframe("\\text{Isolate variables}", 1.2, 0.4);
            side_.add_keyframe("\\text{Square both sides}", 1.2, 0.4);
            side_.add_keyframe("\\text{Back-substitute}", 1.2, 0.4);
            side_.add_keyframe("\\text{Solve cubic}", 1.2, 0.4);

            auto st1 = main_.prepare(*ctx.latex, ctx.height);
            auto st2 = side_.prepare(*ctx.latex, ctx.height);
            if (!st1.ok)
                PANIM_LOG_ERROR("LatexDemo main prepare failed: {}", st1.message);
            if (!st2.ok)
                PANIM_LOG_ERROR("LatexDemo side prepare failed: {}", st2.message);
        }

        void render_frame(Frame &frame, double t) override {
            // gradient background
            for (int y = 0; y < frame.height; ++y) {
                double ny = static_cast<double>(y) / frame.height;
                for (int x = 0; x < frame.width; ++x) {
                    double nx = static_cast<double>(x) / frame.width;
                    double c = 0.5 + 0.5 * std::sin(nx * 2.4 + ny * 1.7 + t * 0.2);
                    uint8_t r = static_cast<uint8_t>(30 + 150 * c);
                    uint8_t g = static_cast<uint8_t>(70 + 100 * (1.0 - c));
                    uint8_t b = static_cast<uint8_t>(170 + 70 * (1.0 - c));
                    frame.set_pixel(x, y, r, g, b, 255);
                }
            }
            if (main_.ready()) {
                double tt = std::fmod(t, main_.duration());
                main_.render(frame, tt);
            }
            if (side_.ready()) {
                double tt = std::fmod(t, side_.duration());
                side_.render(frame, tt);
            }
        }

    private:
        AnimationContext ctx_;
        LatexTrack main_;
        LatexTrack side_;
    };

} // namespace

extern "C" Animation *create_animation() { return new LatexDemo(); }
extern "C" void destroy_animation(Animation *a) { delete a; }
