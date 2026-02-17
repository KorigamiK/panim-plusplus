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

    void Painter::clear(Color c) {
        fill_rect(0, 0, frame_.width, frame_.height, c, 1.0f);
    }

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
        int r2 = radius * radius;
        int x0 = std::max(0, cx - radius);
        int y0 = std::max(0, cy - radius);
        int x1 = std::min(frame_.width - 1, cx + radius);
        int y1 = std::min(frame_.height - 1, cy + radius);
        for (int y = y0; y <= y1; ++y) {
            int dy = y - cy;
            for (int x = x0; x <= x1; ++x) {
                int dx = x - cx;
                if (dx * dx + dy * dy <= r2) {
                    uint8_t *p = frame_.pixel_ptr(x, y);
                    blend_pixel(p, c, alpha);
                }
            }
        }
    }

    void Painter::stroke_line(int x0, int y0, int x1, int y1, int thickness, Color c, float alpha) {
        if (alpha <= 0.0f)
            return;
        thickness = std::max(1, thickness);
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;

        while (true) {
            int half = thickness / 2;
            fill_rect(x0 - half, y0 - half, thickness, thickness, c, alpha);
            if (x0 == x1 && y0 == y1)
                break;
            int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
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

} // namespace panim

