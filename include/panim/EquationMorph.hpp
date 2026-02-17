// Simple LaTeX equation morph: crossfades between two rendered SVGs.
#pragma once

#include <filesystem>
#include <string>

#include "Frame.hpp"
#include "LatexRenderer.hpp"
#include "Status.hpp"

namespace panim {

    struct EquationMorph {
        // Provide LaTeX strings to morph between. scale is applied at rasterization.
        Status init(const std::string &from_latex,
                    const std::string &to_latex,
                    LatexRenderer &renderer,
                    double scale = 1.0,
                    int target_height_px = -1);

        // Set desired center position where the morph should be placed when rendering.
        void set_center(int cx, int cy) {
            center_x = cx;
            center_y = cy;
            use_norm_center = false;
        }
        // Normalized center (0..1). If set, overrides pixel center.
        void set_center_norm(double nx, double ny) {
            center_norm_x = nx;
            center_norm_y = ny;
            use_norm_center = true;
        }

        // Render blended equation onto target frame. t in [0,1].
        void render(Frame &target, double t) const;

        bool ready() const { return ready_flag; }
        const std::string &last_error() const { return error; }

    private:
        Frame from_frame{0, 0};
        Frame to_frame{0, 0};
        int center_x = 0;
        int center_y = 0;
        double center_norm_x = 0.5;
        double center_norm_y = 0.8;
        bool use_norm_center = false;
        bool ready_flag = false;
        std::string error;
    };

} // namespace panim
