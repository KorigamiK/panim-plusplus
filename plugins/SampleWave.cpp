#include <cmath>

#include "panim/Animation.hpp"
#include "panim/CudaHelpers.hpp"
#include "panim/EquationMorph.hpp"
#include "panim/Frame.hpp"
#include "panim/LatexRenderer.hpp"
#include "panim/Log.hpp"
#include "panim/SvgRenderer.hpp"

using namespace panim;

namespace {

    class SampleWave : public Animation {
    public:
        void on_setup(const AnimationContext &ctx) override {
            ctx_ = ctx;
            if (ctx.latex) {
                morph_.init("\\int_{-\\infty}^{\\infty} e^{-x^2} dx = \\sqrt{\\pi}",
                            "\\frac{1}{\\sqrt{\\pi}} e^{-x^2} \\longrightarrow 1",
                            *ctx.latex,
                            1.0,
                            static_cast<int>(ctx.height * 0.28));
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
                double phase = 0.5 + 0.5 * std::sin(t * 1.6);
                morph_.render(frame, phase);
            }

            static bool gpu_reported = false;
            bool gpu_ok = gpu_invert(frame);
            if (!gpu_reported) {
                if (gpu_ok) {
                    PANIM_LOG_INFO("GPU invert applied");
                } else {
                    PANIM_LOG_WARN("CUDA not enabled; frame stays CPU-rendered");
                }
                gpu_reported = true;
            }
        }

    private:
        AnimationContext ctx_;
        EquationMorph morph_;
    };

} // namespace

extern "C" Animation *create_animation() {
    return new SampleWave();
}

extern "C" void destroy_animation(Animation *anim) {
    delete anim;
}
