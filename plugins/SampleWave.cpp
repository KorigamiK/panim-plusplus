#include <algorithm>
#include <cmath>

#include "panim/Animation.hpp"
#include "panim/EquationMorph.hpp"
#include "panim/Frame.hpp"
#include "panim/LatexRenderer.hpp"
#include "panim/Plugin.hpp"

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
        AnimationInfo info() const override { return {"SampleWave", 6.0, 1280, 720, 60.0}; }

        void on_setup(const AnimationContext &ctx) override {
            ctx_ = ctx;
            if (ctx.latex) {
                morph_.init("\\int_{-\\infty}^{\\infty} e^{-x^2} dx = \\sqrt{\\pi}",
                            "\\frac{1}{\\sqrt{\\pi}} \\int_{-\\infty}^{\\infty} "
                            "e^{-x^2} dx = 1",
                            *ctx.latex,
                            1.0,
                            static_cast<int>(ctx.height * 0.18));
                morph_.set_center_norm(0.5, 0.6); // near center
                morph_.set_tint(245, 247, 255);
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

            if (morph_.ready()) {
                morph_.render(frame, equation_phase(t));
            }
        }

    private:
        AnimationContext ctx_;
        EquationMorph morph_;
    };

} // namespace

PANIM_EXPORT_ANIMATION(SampleWave)
