// Basic RGBA frame utilities.
#include "panim/Frame.hpp"

#include <cassert>
#include <fstream>

namespace panim {

    Frame::Frame(int w, int h) : width(w), height(h), pixels(static_cast<size_t>(w * h * 4), 0) {}

    void Frame::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                uint8_t *p = pixel_ptr(x, y);
                p[0] = r;
                p[1] = g;
                p[2] = b;
                p[3] = a;
            }
        }
    }

    void Frame::set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        uint8_t *p = pixel_ptr(x, y);
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = a;
    }

    uint8_t *Frame::pixel_ptr(int x, int y) {
        assert(x >= 0 && y >= 0 && x < width && y < height);
        size_t idx = static_cast<size_t>((y * width + x) * 4);
        return pixels.data() + idx;
    }

    void write_ppm(const Frame &frame, const std::string &path) {
        std::ofstream out(path, std::ios::binary);
        out << "P6\n"
            << frame.width << " " << frame.height << "\n255\n";
        for (int y = 0; y < frame.height; ++y) {
            for (int x = 0; x < frame.width; ++x) {
                const uint8_t *p = frame.pixels.data() + static_cast<size_t>((y * frame.width + x) * 4);
                out.put(static_cast<char>(p[0]));
                out.put(static_cast<char>(p[1]));
                out.put(static_cast<char>(p[2]));
            }
        }
    }

} // namespace panim
