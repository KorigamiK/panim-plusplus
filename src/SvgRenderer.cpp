#include "panim/SvgRenderer.hpp"
#include "panim/Log.hpp"

#ifdef PANIM_ENABLE_SVG

#include <algorithm>
#include <cairo/cairo.h>
#include <cmath>
#include <librsvg/rsvg.h>
#include <vector>

namespace panim {

    namespace {

        struct SvgImage {
            RsvgHandle *handle = nullptr;
            double width = 0.0;
            double height = 0.0;

            SvgImage() = default;
            SvgImage(const SvgImage &) = delete;
            SvgImage &operator=(const SvgImage &) = delete;

            SvgImage(SvgImage &&other) noexcept : handle(other.handle), width(other.width), height(other.height) { other.handle = nullptr; }

            ~SvgImage() {
                if (handle)
                    g_object_unref(handle);
            }
        };

        bool load_dimensions(SvgImage &img, const std::string &label) {
            gdouble w = 0.0;
            gdouble h = 0.0;
            if (!rsvg_handle_get_intrinsic_size_in_pixels(img.handle, &w, &h)) {
                PANIM_LOG_ERROR("SVG has no intrinsic size: {}", label);
                return false;
            }
            img.width = w;
            img.height = h;
            return true;
        }

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

            if (!load_dimensions(img, path.string())) {
                g_object_unref(img.handle);
                img.handle = nullptr;
            }
            return img;
        }

        SvgImage load_svg_data(std::string_view data) {
            GError *err = nullptr;
            SvgImage img;
            img.handle = rsvg_handle_new_from_data(reinterpret_cast<const guint8 *>(data.data()), data.size(), &err);
            if (!img.handle) {
                std::string msg = err ? err->message : "unknown error";
                if (err)
                    g_error_free(err);
                PANIM_LOG_ERROR("Failed to load in-memory SVG: {}", msg);
                return img;
            }

            if (!load_dimensions(img, "in-memory SVG")) {
                g_object_unref(img.handle);
                img.handle = nullptr;
            }
            return img;
        }

        void alpha_blend(Frame &dst, int dst_x, int dst_y, const std::vector<uint8_t> &src, int src_w, int src_h, int src_stride) {
            for (int y = 0; y < src_h; ++y) {
                int fy = dst_y + y;
                if (fy < 0 || fy >= dst.height)
                    continue;
                for (int x = 0; x < src_w; ++x) {
                    int fx = dst_x + x;
                    if (fx < 0 || fx >= dst.width)
                        continue;
                    size_t si = static_cast<size_t>(y * src_stride + x * 4);
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

        bool render_image_to_frame(SvgImage &img, Frame &frame, int dst_x, int dst_y, double scale) {
            int out_w = static_cast<int>(std::lround(img.width * scale));
            int out_h = static_cast<int>(std::lround(img.height * scale));
            if (out_w <= 0 || out_h <= 0)
                return false;

            int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, out_w);
            std::vector<uint8_t> raw(static_cast<size_t>(stride * out_h), 0);

            cairo_surface_t *surface = cairo_image_surface_create_for_data(raw.data(), CAIRO_FORMAT_ARGB32, out_w, out_h, stride);
            cairo_t *cr = cairo_create(surface);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

            // librsvg scales the document into the supplied viewport. Using the
            // intrinsic size here only painted an unscaled equation into the
            // top-left corner of a larger transparent surface.
            RsvgRectangle viewport{0, 0, static_cast<double>(out_w), static_cast<double>(out_h)};
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

            cairo_surface_flush(surface);
            cairo_destroy(cr);
            cairo_surface_destroy(surface);

            // CAIRO ARGB32 is premultiplied BGRA on little-endian systems.
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

            alpha_blend(frame, dst_x, dst_y, raw, out_w, out_h, stride);
            return true;
        }

        bool rasterize_image(SvgImage &img, Frame &out, double scale) {
            int width = static_cast<int>(std::lround(img.width * scale));
            int height = static_cast<int>(std::lround(img.height * scale));
            if (width <= 0 || height <= 0)
                return false;

            Frame frame(width, height);
            if (!render_image_to_frame(img, frame, 0, 0, scale))
                return false;
            out = std::move(frame);
            return true;
        }

    } // namespace

    bool render_svg_to_frame(const std::filesystem::path &svg_path, Frame &frame, int dst_x, int dst_y, double scale) {
        if (!std::filesystem::exists(svg_path))
            return false;
        SvgImage img = load_svg(svg_path);
        if (!img.handle)
            return false;
        return render_image_to_frame(img, frame, dst_x, dst_y, scale);
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
        if (!std::filesystem::exists(svg_path))
            return false;
        SvgImage img = load_svg(svg_path);
        if (!img.handle)
            return false;
        return rasterize_image(img, out, scale);
    }

    bool rasterize_svg_data(std::string_view svg_data, Frame &out, double scale) {
        SvgImage img = load_svg_data(svg_data);
        if (!img.handle)
            return false;
        return rasterize_image(img, out, scale);
    }

} // namespace panim

#endif // PANIM_ENABLE_SVG
