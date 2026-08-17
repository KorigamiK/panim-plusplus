// Rasterize SVGs (e.g., MicroTeX output) into a Frame buffer.
#pragma once

#include <filesystem>
#include <string_view>

#include "Frame.hpp"

namespace panim {

#ifdef PANIM_ENABLE_SVG
    // Render an SVG image onto the destination frame at (dst_x, dst_y).
    // Returns true on success. On failure, the frame is left unchanged.
    bool render_svg_to_frame(const std::filesystem::path &svg_path,
                             Frame &frame,
                             int dst_x,
                             int dst_y,
                             double scale = 1.0);

    // Query intrinsic pixel dimensions of an SVG. Returns false on failure.
    bool svg_dimensions(const std::filesystem::path &svg_path, int &w, int &h);

    // Rasterize an SVG into an output frame (frame is resized). Returns false on failure.
    bool rasterize_svg(const std::filesystem::path &svg_path, Frame &out, double scale = 1.0);

    // Rasterize SVG markup held in memory. This is used for independently
    // compositing MicroTeX glyph layers without temporary files.
    bool rasterize_svg_data(std::string_view svg_data, Frame &out, double scale = 1.0);
#else
    inline bool render_svg_to_frame(const std::filesystem::path &, Frame &, int, int, double = 1.0) { return false; }
    inline bool svg_dimensions(const std::filesystem::path &, int &, int &) { return false; }
    inline bool rasterize_svg(const std::filesystem::path &, Frame &, double = 1.0) { return false; }
    inline bool rasterize_svg_data(std::string_view, Frame &, double = 1.0) { return false; }
#endif

} // namespace panim
