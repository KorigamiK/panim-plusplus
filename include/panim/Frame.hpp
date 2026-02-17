// Core frame representation for simple RGBA buffers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace panim {

    struct Frame {
        int width;
        int height;
        std::vector<uint8_t> pixels; // RGBA, row-major

        Frame(int w, int h);

        void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
        void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
        uint8_t *pixel_ptr(int x, int y);
    };

    // Minimal PPM writer for quick smoketests.
    // Output is 24-bit RGB regardless of source alpha.
    void write_ppm(const Frame &frame, const std::string &path);

} // namespace panim
