#pragma once

#include "panim/Color.hpp"
#include "panim/Frame.hpp"

namespace panim {

    // Small helper for drawing basic shapes directly into a Frame.
    class Painter {
    public:
        explicit Painter(Frame &frame) : frame_(frame) {}

        void clear(Color c);
        void fill_rect(int x, int y, int w, int h, Color c, float alpha = 1.0f);
        void fill_vertical_gradient(Color top, Color bottom);
        void fill_circle(int cx, int cy, int radius, Color c, float alpha = 1.0f);
        void stroke_line(int x0, int y0, int x1, int y1, int thickness, Color c, float alpha = 1.0f);
        void blit(const Frame &src, int dst_x, int dst_y, float opacity = 1.0f);
        void blit_scaled(const Frame &src,
                         int dst_x,
                         int dst_y,
                         int dst_width,
                         int dst_height,
                         float opacity = 1.0f);

    private:
        Frame &frame_;
    };

} // namespace panim
