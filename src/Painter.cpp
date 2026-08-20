#include "panim/Painter.hpp"

#include <algorithm>
#include <cmath>

namespace panim {

    namespace {
        inline void blend_pixel(uint8_t *dst, Color c, float alpha) {
            float a = (c.a / 255.0f) * alpha;
            if (a <= 0.0f)
                return;
            float inv = 1.0f - a;
            dst[0] = static_cast<uint8_t>(c.r * a + dst[0] * inv);
            dst[1] = static_cast<uint8_t>(c.g * a + dst[1] * inv);
            dst[2] = static_cast<uint8_t>(c.b * a + dst[2] * inv);
            dst[3] = 255;
        }
    } // namespace

    void Painter::clear(Color c) { frame_.clear(c.r, c.g, c.b, c.a); }

    void Painter::fill_rect(int x, int y, int w, int h, Color c, float alpha) {
        if (w <= 0 || h <= 0 || alpha <= 0.0f)
            return;
        int x0 = std::max(0, x);
        int y0 = std::max(0, y);
        int x1 = std::min(frame_.width, x + w);
        int y1 = std::min(frame_.height, y + h);
        for (int yy = y0; yy < y1; ++yy) {
            size_t row_off = static_cast<size_t>(yy * frame_.width * 4);
            for (int xx = x0; xx < x1; ++xx) {
                uint8_t *p = frame_.pixels.data() + row_off + static_cast<size_t>(xx * 4);
                blend_pixel(p, c, alpha);
            }
        }
    }

    void Painter::fill_vertical_gradient(Color top, Color bottom) {
        if (frame_.height <= 1) {
            clear(top);
            return;
        }
        int w = frame_.width;
        int h = frame_.height;
        for (int y = 0; y < h; ++y) {
            float t = static_cast<float>(y) / static_cast<float>(h - 1);
            Color c = lerp_color(top, bottom, t);
            size_t row_off = static_cast<size_t>(y * w * 4);
            for (int x = 0; x < w; ++x) {
                uint8_t *p = frame_.pixels.data() + row_off + static_cast<size_t>(x * 4);
                p[0] = c.r;
                p[1] = c.g;
                p[2] = c.b;
                p[3] = c.a;
            }
        }
    }

    void Painter::fill_circle(int cx, int cy, int radius, Color c, float alpha) {
        if (radius <= 0 || alpha <= 0.0f)
            return;
        int x0 = std::max(0, cx - radius - 1);
        int y0 = std::max(0, cy - radius - 1);
        int x1 = std::min(frame_.width - 1, cx + radius + 1);
        int y1 = std::min(frame_.height - 1, cy + radius + 1);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                double dx = static_cast<double>(x - cx);
                double dy = static_cast<double>(y - cy);
                double distance = std::sqrt(dx * dx + dy * dy);
                float coverage = static_cast<float>(std::clamp(radius + 0.5 - distance, 0.0, 1.0));
                if (coverage > 0.0f) {
                    uint8_t *p = frame_.pixel_ptr(x, y);
                    blend_pixel(p, c, alpha * coverage);
                }
            }
        }
    }

    void Painter::stroke_line(int x0, int y0, int x1, int y1, int thickness, Color c, float alpha) {
        if (alpha <= 0.0f)
            return;
        thickness = std::max(1, thickness);

        double ax = static_cast<double>(x0);
        double ay = static_cast<double>(y0);
        double bx = static_cast<double>(x1);
        double by = static_cast<double>(y1);
        double vx = bx - ax;
        double vy = by - ay;
        double length_sq = vx * vx + vy * vy;
        double half_width = thickness * 0.5;
        int padding = static_cast<int>(std::ceil(half_width + 1.0));
        int min_x = std::max(0, std::min(x0, x1) - padding);
        int min_y = std::max(0, std::min(y0, y1) - padding);
        int max_x = std::min(frame_.width - 1, std::max(x0, x1) + padding);
        int max_y = std::min(frame_.height - 1, std::max(y0, y1) + padding);

        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                double projection = 0.0;
                if (length_sq > 0.0) {
                    projection = ((x - ax) * vx + (y - ay) * vy) / length_sq;
                    projection = std::clamp(projection, 0.0, 1.0);
                }
                double closest_x = ax + projection * vx;
                double closest_y = ay + projection * vy;
                double dx = x - closest_x;
                double dy = y - closest_y;
                double distance = std::sqrt(dx * dx + dy * dy);
                float coverage = static_cast<float>(std::clamp(half_width + 0.5 - distance, 0.0, 1.0));
                if (coverage > 0.0f) {
                    blend_pixel(frame_.pixel_ptr(x, y), c, alpha * coverage);
                }
            }
        }
    }

    void Painter::blit(const Frame &src, int dst_x, int dst_y, float opacity) {
        if (opacity <= 0.0f)
            return;
        for (int y = 0; y < src.height; ++y) {
            int fy = dst_y + y;
            if (fy < 0 || fy >= frame_.height)
                continue;
            for (int x = 0; x < src.width; ++x) {
                int fx = dst_x + x;
                if (fx < 0 || fx >= frame_.width)
                    continue;
                const uint8_t *sp = src.pixels.data() + static_cast<size_t>((y * src.width + x) * 4);
                uint8_t *dp = frame_.pixel_ptr(fx, fy);
                Color c{sp[0], sp[1], sp[2], sp[3]};
                blend_pixel(dp, c, opacity);
            }
        }
    }

    void Painter::blit_scaled(const Frame &src, int dst_x, int dst_y, int dst_width, int dst_height, float opacity) {
        if (src.width <= 0 || src.height <= 0 || dst_width <= 0 || dst_height <= 0 || opacity <= 0.0f) {
            return;
        }

        int x_begin = std::max(0, dst_x);
        int y_begin = std::max(0, dst_y);
        int x_end = std::min(frame_.width, dst_x + dst_width);
        int y_end = std::min(frame_.height, dst_y + dst_height);
        for (int y = y_begin; y < y_end; ++y) {
            double source_y = ((y - dst_y) + 0.5) * src.height / static_cast<double>(dst_height) - 0.5;
            int y0 = std::clamp(static_cast<int>(std::floor(source_y)), 0, src.height - 1);
            int y1 = std::min(y0 + 1, src.height - 1);
            double fy = std::clamp(source_y - std::floor(source_y), 0.0, 1.0);

            for (int x = x_begin; x < x_end; ++x) {
                double source_x = ((x - dst_x) + 0.5) * src.width / static_cast<double>(dst_width) - 0.5;
                int x0 = std::clamp(static_cast<int>(std::floor(source_x)), 0, src.width - 1);
                int x1 = std::min(x0 + 1, src.width - 1);
                double fx = std::clamp(source_x - std::floor(source_x), 0.0, 1.0);

                const uint8_t *p00 = src.pixel_ptr(x0, y0);
                const uint8_t *p10 = src.pixel_ptr(x1, y0);
                const uint8_t *p01 = src.pixel_ptr(x0, y1);
                const uint8_t *p11 = src.pixel_ptr(x1, y1);
                Color sample;
                uint8_t *channels[] = {&sample.r, &sample.g, &sample.b, &sample.a};
                for (int channel = 0; channel < 4; ++channel) {
                    double top = p00[channel] + (p10[channel] - p00[channel]) * fx;
                    double bottom = p01[channel] + (p11[channel] - p01[channel]) * fx;
                    *channels[channel] = static_cast<uint8_t>(std::lround(top + (bottom - top) * fy));
                }
                blend_pixel(frame_.pixel_ptr(x, y), sample, opacity);
            }
        }
    }

} // namespace panim
