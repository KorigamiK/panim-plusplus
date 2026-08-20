// Glyph-aware transition between two MicroTeX-rendered equations.
#pragma once

#include <string>
#include <vector>

#include "Frame.hpp"
#include "LatexRenderer.hpp"
#include "Status.hpp"

namespace panim {

    struct EquationMorphSizing {
        double from_scale = 1.0;
        double to_scale = 1.0;
        int from_height_px = -1;
        int to_height_px = -1;
    };

    struct EquationMorph {
        // Provide LaTeX strings to morph between. scale is applied at rasterization.
        Status init(const std::string &from_latex, const std::string &to_latex, LatexRenderer &renderer, double scale = 1.0,
                    int target_height_px = -1);

        // Use independent endpoint heights when the animation intentionally
        // changes text size. A negative height leaves that endpoint at scale.
        Status init(const std::string &from_latex, const std::string &to_latex, LatexRenderer &renderer, const EquationMorphSizing &sizing);

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
        void set_tint(uint8_t r, uint8_t g, uint8_t b) {
            tint_r = r;
            tint_g = g;
            tint_b = b;
            tint_set = true;
        }

        // Math matches glyph outlines and moves them to their new positions.
        // Pure \text{} phrases use a size-aware push dissolve so unrelated
        // sentences remain legible through the transition. t is in [0, 1].
        void render(Frame &target, double t) const;

        bool ready() const { return ready_flag; }
        const std::string &last_error() const { return error; }

    private:
        struct GlyphLayer {
            Frame frame{0, 0};
            int offset_x = 0;
            int offset_y = 0;
            int match_index = -1;
            int pair_index = -1;
            bool matched = false;
            bool paired = false;
        };

        Frame from_frame{0, 0};
        Frame to_frame{0, 0};
        Frame from_content_frame{0, 0};
        Frame to_content_frame{0, 0};
        std::vector<GlyphLayer> from_layers;
        std::vector<GlyphLayer> to_layers;
        int from_content_width = 0;
        int from_content_height = 0;
        int to_content_width = 0;
        int to_content_height = 0;
        int center_x = 0;
        int center_y = 0;
        double center_norm_x = 0.5;
        double center_norm_y = 0.8;
        bool use_norm_center = false;
        bool matching_ready = false;
        bool text_transition = false;
        bool ready_flag = false;
        uint8_t tint_r = 255;
        uint8_t tint_g = 255;
        uint8_t tint_b = 255;
        bool tint_set = false;
        std::string error;
    };

} // namespace panim
