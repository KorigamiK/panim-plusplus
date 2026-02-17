#include "panim/EquationMorph.hpp"
#include "panim/SvgRenderer.hpp"

namespace panim {

    namespace {

        // Blend two frames of equal size into out (also equal size).
        void blend_frames(const Frame &a, const Frame &b, Frame &out, double t) {
            const double clamped = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
            const int size = a.width * a.height;
            for (int i = 0; i < size; ++i) {
                const uint8_t *pa = a.pixels.data() + i * 4;
                const uint8_t *pb = b.pixels.data() + i * 4;
                uint8_t *po = out.pixels.data() + i * 4;

                double aA = pa[3] / 255.0;
                double bA = pb[3] / 255.0;
                double mixA = (1.0 - clamped) * aA + clamped * bA;

                double r = (1.0 - clamped) * (pa[0] * aA) + clamped * (pb[0] * bA);
                double g = (1.0 - clamped) * (pa[1] * aA) + clamped * (pb[1] * bA);
                double bch = (1.0 - clamped) * (pa[2] * aA) + clamped * (pb[2] * bA);

                if (mixA > 0.0001) {
                    po[0] = static_cast<uint8_t>(r / mixA);
                    po[1] = static_cast<uint8_t>(g / mixA);
                    po[2] = static_cast<uint8_t>(bch / mixA);
                    po[3] = static_cast<uint8_t>(mixA * 255.0);
                } else {
                    po[0] = po[1] = po[2] = 0;
                    po[3] = 0;
                }
            }
        }

        // Alpha blend src onto dst in-place.
        void alpha_over(const Frame &src, Frame &dst, int x0, int y0) {
            for (int y = 0; y < src.height; ++y) {
                int dy = y0 + y;
                if (dy < 0 || dy >= dst.height)
                    continue;
                for (int x = 0; x < src.width; ++x) {
                    int dx = x0 + x;
                    if (dx < 0 || dx >= dst.width)
                        continue;
                    const uint8_t *sp = src.pixels.data() + (y * src.width + x) * 4;
                    uint8_t *dp = dst.pixel_ptr(dx, dy);

                    double a = sp[3] / 255.0;
                    dp[0] = static_cast<uint8_t>(sp[0] * a + dp[0] * (1.0 - a));
                    dp[1] = static_cast<uint8_t>(sp[1] * a + dp[1] * (1.0 - a));
                    dp[2] = static_cast<uint8_t>(sp[2] * a + dp[2] * (1.0 - a));
                    dp[3] = 255;
                }
            }
        }

        // Pad frame to target size.
        Frame pad_to(const Frame &src, int w, int h) {
            Frame out(w, h);
            out.clear(0, 0, 0, 0);
            int ox = (w - src.width) / 2;
            int oy = (h - src.height) / 2;
            for (int y = 0; y < src.height; ++y) {
                for (int x = 0; x < src.width; ++x) {
                    const uint8_t *sp = src.pixels.data() + (y * src.width + x) * 4;
                    uint8_t *dp = out.pixel_ptr(ox + x, oy + y);
                    dp[0] = sp[0];
                    dp[1] = sp[1];
                    dp[2] = sp[2];
                    dp[3] = sp[3];
                }
            }
            return out;
        }

    } // namespace

    Status EquationMorph::init(const std::string &from_latex,
                               const std::string &to_latex,
                               LatexRenderer &renderer,
                               double scale,
                               int target_height_px) {
        ready_flag = false;
        error.clear();

        std::filesystem::path svg_from;
        std::filesystem::path svg_to;
        auto st_a = renderer.render_svg(from_latex, svg_from);
        if (!st_a.ok) {
            error = st_a.message;
            return Status::failure(error);
        }
        auto st_b = renderer.render_svg(to_latex, svg_to);
        if (!st_b.ok) {
            error = st_b.message;
            return Status::failure(error);
        }

        // If a target height is requested, recompute scale based on intrinsic SVG height.
        if (target_height_px > 0) {
            int w0 = 0, h0 = 0;
            if (svg_dimensions(svg_from, w0, h0) && h0 > 0) {
                scale = static_cast<double>(target_height_px) / static_cast<double>(h0);
            }
        }

        Frame ra(1, 1), rb(1, 1);
        if (!rasterize_svg(svg_from, ra, scale)) {
            error = "rasterize from failed";
            return Status::failure(error);
        }
        if (!rasterize_svg(svg_to, rb, scale)) {
            error = "rasterize to failed";
            return Status::failure(error);
        }

        int out_w = std::max(ra.width, rb.width);
        int out_h = std::max(ra.height, rb.height);
        from_frame = pad_to(ra, out_w, out_h);
        to_frame = pad_to(rb, out_w, out_h);

        ready_flag = true;
        return Status::success();
    }

    void EquationMorph::render(Frame &target, double t) const {
        if (!ready_flag)
            return;
        Frame blended(from_frame.width, from_frame.height);
        blended.clear(0, 0, 0, 0);
        blend_frames(from_frame, to_frame, blended, t);

        int cx = center_x;
        int cy = center_y;
        if (use_norm_center) {
            cx = static_cast<int>(center_norm_x * target.width);
            cy = static_cast<int>(center_norm_y * target.height);
        }
        int x = cx - blended.width / 2;
        int y = cy - blended.height / 2;
        alpha_over(blended, target, x, y);
    }

} // namespace panim
