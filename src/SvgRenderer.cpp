#include "panim/SvgRenderer.hpp"
#include "panim/Log.hpp"

#ifdef PANIM_ENABLE_SVG

#include <cairo/cairo.h>
#include <librsvg/rsvg.h>
#include <vector>

namespace panim {

    namespace {

        struct SvgImage {
            RsvgHandle *handle = nullptr;
            double width = 0.0;
            double height = 0.0;

            ~SvgImage() {
                if (handle)
                    g_object_unref(handle);
            }
        };

        SvgImage load_svg(const std::filesystem::path &path) {
            GError *err = nullptr;
            SvgImage img;
            img.handle = rsvg_handle_new_from_file(path.c_str(), &err);
            if (!img.handle) {
                std::string msg = err ? err->message : "unknown error";
                if (err)
                    g_error_free(err);
                PANIM_LOG_ERROR("Failed to load SVG: {}", msg);
                return img;
            }

            gdouble w = 0, h = 0;
            if (!rsvg_handle_get_intrinsic_size_in_pixels(img.handle, &w, &h)) {
                PANIM_LOG_ERROR("SVG has no intrinsic size: {}", path.string());
                return img;
            }
            img.width = w;
            img.height = h;
            return img;
        }

        void alpha_blend(Frame &dst, int dst_x, int dst_y,
                         const std::vector<uint8_t> &src, int src_w, int src_h) {
            for (int y = 0; y < src_h; ++y) {
                int fy = dst_y + y;
                if (fy < 0 || fy >= dst.height)
                    continue;
                for (int x = 0; x < src_w; ++x) {
                    int fx = dst_x + x;
                    if (fx < 0 || fx >= dst.width)
                        continue;
                    size_t si = static_cast<size_t>((y * src_w + x) * 4);
                    uint8_t sr = src[si + 0];
                    uint8_t sg = src[si + 1];
                    uint8_t sb = src[si + 2];
                    uint8_t sa = src[si + 3];

                    uint8_t *dp = dst.pixel_ptr(fx, fy);
                    uint8_t dr = dp[0];
                    uint8_t dg = dp[1];
                    uint8_t db = dp[2];

                    float a = sa / 255.0f;
                    if (a < 1e-4f)
                        continue;
                    dp[0] = static_cast<uint8_t>(sr * a + dr * (1.0f - a));
                    dp[1] = static_cast<uint8_t>(sg * a + dg * (1.0f - a));
                    dp[2] = static_cast<uint8_t>(sb * a + db * (1.0f - a));
                    dp[3] = 255;
                }
            }
        }

        // Remove likely light background using corner sampling; skip if background is dark/transparent.
        void remove_uniform_bg(std::vector<uint8_t> &raw, int w, int h) {
            if (raw.empty())
                return;
            auto sample = [&](int x, int y) {
                size_t idx = static_cast<size_t>((y * w + x) * 4);
                return std::array<uint8_t, 4>{raw[idx], raw[idx + 1], raw[idx + 2], raw[idx + 3]};
            };
            std::array<uint8_t, 4> corners[] = {
                sample(0, 0), sample(w - 1, 0), sample(0, h - 1), sample(w - 1, h - 1)};
            int br = 0, bg = 0, bb = 0, ba = 0;
            for (auto &c : corners) {
                br += c[0];
                bg += c[1];
                bb += c[2];
                ba += c[3];
            }
            br /= 4;
            bg /= 4;
            bb /= 4;
            ba /= 4;
            int brightness = (br + bg + bb) / 3;
            if (brightness < 220 || ba < 200) {
                // background already dark/transparent; don't strip to avoid nuking strokes
                return;
            }
            const int tol = 20;
            for (size_t i = 0; i + 3 < raw.size(); i += 4) {
                uint8_t r = raw[i], g = raw[i + 1], b = raw[i + 2], a = raw[i + 3];
                int lum = (r + g + b) / 3;
                if (a > 200 &&
                    std::abs(int(r) - br) <= tol &&
                    std::abs(int(g) - bg) <= tol &&
                    std::abs(int(b) - bb) <= tol &&
                    lum > 220) {
                    raw[i + 3] = 0;
                }
            }
        }

    } // namespace

    bool render_svg_to_frame(const std::filesystem::path &svg_path,
                             Frame &frame,
                             int dst_x,
                             int dst_y,
                             double scale) {
        if (!std::filesystem::exists(svg_path))
            return false;
        SvgImage img = load_svg(svg_path);
        if (!img.handle)
            return false;

        int out_w = static_cast<int>(img.width * scale);
        int out_h = static_cast<int>(img.height * scale);
        if (out_w <= 0 || out_h <= 0)
            return false;

        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, out_w);
        std::vector<uint8_t> raw(static_cast<size_t>(stride * out_h), 0);

        cairo_surface_t *surface = cairo_image_surface_create_for_data(
            raw.data(), CAIRO_FORMAT_ARGB32, out_w, out_h, stride);
        cairo_t *cr = cairo_create(surface);

        // Surface is already scaled to desired output; no extra scale here.
        RsvgRectangle viewport{0, 0, img.width, img.height};
        GError *err = nullptr;
        if (!rsvg_handle_render_document(img.handle, cr, &viewport, &err)) {
            if (err) {
                PANIM_LOG_ERROR("rsvg render failed: {}", err->message);
            } else {
                PANIM_LOG_ERROR("rsvg render failed: unknown error");
            }
            cairo_destroy(cr);
            cairo_surface_destroy(surface);
            if (err)
                g_error_free(err);
            return false;
        }

        cairo_destroy(cr);
        cairo_surface_destroy(surface);

        // Convert from CAIRO ARGB32 (premultiplied, stored BGRA on little-endian) to straight RGBA.
        for (int y = 0; y < out_h; ++y) {
            for (int x = 0; x < out_w; ++x) {
                size_t idx = static_cast<size_t>(y * stride + x * 4);
                uint8_t b = raw[idx + 0];
                uint8_t g = raw[idx + 1];
                uint8_t r = raw[idx + 2];
                uint8_t a = raw[idx + 3];
                if (a > 0) {
                    float inv = 255.0f / a;
                    r = static_cast<uint8_t>(std::min(255.0f, r * inv));
                    g = static_cast<uint8_t>(std::min(255.0f, g * inv));
                    b = static_cast<uint8_t>(std::min(255.0f, b * inv));
                }
                raw[idx + 0] = r;
                raw[idx + 1] = g;
                raw[idx + 2] = b;
                raw[idx + 3] = a;
            }
        }

        // If MicroTeX already renders with transparent background, no scrub is needed.
        // Background scrub can be re-enabled if assets carry opaque backdrops.
        // remove_uniform_bg(raw, out_w, out_h);

        alpha_blend(frame, dst_x, dst_y, raw, out_w, out_h);
        return true;
    }

    bool svg_dimensions(const std::filesystem::path &svg_path, int &w, int &h) {
        SvgImage img = load_svg(svg_path);
        if (!img.handle)
            return false;
        w = static_cast<int>(img.width);
        h = static_cast<int>(img.height);
        return true;
    }

    bool rasterize_svg(const std::filesystem::path &svg_path, Frame &out, double scale) {
        int w = 0, h = 0;
        if (!svg_dimensions(svg_path, w, h))
            return false;
        w = static_cast<int>(w * scale);
        h = static_cast<int>(h * scale);
        if (w <= 0 || h <= 0)
            return false;
        Frame tmp(w, h);
        if (!render_svg_to_frame(svg_path, tmp, 0, 0, scale))
            return false;
        out = std::move(tmp);
        return true;
    }

} // namespace panim

#endif // PANIM_ENABLE_SVG
