#include <algorithm>
#include <cmath>

#include "panim/Animation.hpp"
#include "panim/Compute.hpp"
#include "panim/EquationMorph.hpp"
#include "panim/Frame.hpp"
#include "panim/LatexRenderer.hpp"
#include "panim/Log.hpp"
#include "panim/Plugin.hpp"
#include "panim/SvgRenderer.hpp"

using namespace panim;

namespace {

    double smoothstep(double t) {
        t = std::clamp(t, 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    double equation_phase(double t) {
        double cycle = std::fmod(t, 6.0);
        if (cycle < 1.5)
            return 0.0;
        if (cycle < 2.25)
            return smoothstep((cycle - 1.5) / 0.75);
        if (cycle < 4.5)
            return 1.0;
        if (cycle < 5.25)
            return 1.0 - smoothstep((cycle - 4.5) / 0.75);
        return 0.0;
    }

    class SampleWave : public Animation {
    public:
        AnimationInfo info() const override { return {"SampleWave", 6.0, 1280, 720, 30.0}; }

        void on_setup(const AnimationContext &ctx) override {
            ctx_ = ctx;
            if (ctx.latex) {
                morph_.init("\\int_{-\\infty}^{\\infty} e^{-x^2} dx = \\sqrt{\\pi}", "\\frac{1}{\\sqrt{\\pi}} e^{-x^2} \\longrightarrow 1",
                            *ctx.latex, 1.0, static_cast<int>(ctx.height * 0.15));
                morph_.set_center_norm(0.5, 0.6); // near center
            }
        }

        void render_frame(Frame &frame, double t) override {
            const double freq = 0.8;
            const double speed = 1.8;
            for (int y = 0; y < ctx_.height; ++y) {
                double ny = static_cast<double>(y) / ctx_.height;
                for (int x = 0; x < ctx_.width; ++x) {
                    double nx = static_cast<double>(x) / ctx_.width;
                    double wave = 0.5 + 0.5 * std::sin((nx * freq + t * speed) * 6.28318 + ny * 3.14159);
                    uint8_t r = static_cast<uint8_t>(40 + 200 * wave);
                    uint8_t g = static_cast<uint8_t>(120 + 80 * wave);
                    uint8_t b = static_cast<uint8_t>(200 - 160 * wave);
                    frame.set_pixel(x, y, r, g, b, 255);
                }
            }

            // Draw a small marker that marches across the screen.
            int marker_x = static_cast<int>(std::fmod(t * ctx_.width * 0.5, ctx_.width - 50));
            for (int dy = 0; dy < 32; ++dy) {
                for (int dx = 0; dx < 50; ++dx) {
                    frame.set_pixel(marker_x + dx, 30 + dy, 18, 18, 18, 255);
                }
            }

            if (morph_.ready()) {
                morph_.render(frame, equation_phase(t));
            }

            ComputeParams params;
            auto result = apply_compute_effect(frame, ComputeEffect::Invert, params);
            if (!compute_reported_) {
                if (result.ok) {
                    PANIM_LOG_INFO("{} invert applied", compute_backend_name(result.backend));
                } else {
                    PANIM_LOG_WARN("Compute invert unavailable: {}", result.message);
                }
                compute_reported_ = true;
            }
        }

    private:
        AnimationContext ctx_;
        EquationMorph morph_;
        bool compute_reported_ = false;
    };

} // namespace

PANIM_EXPORT_ANIMATION(SampleWave)
