// Small helper around MicroTeX / LaTeX rendering to SVG assets.
#pragma once

#include <filesystem>
#include <string>

#include "Status.hpp"

namespace panim {

    class LatexRenderer {
    public:
        explicit LatexRenderer(std::filesystem::path scratch_dir = "out/latex");

        // Render a LaTeX snippet to an SVG file and write path into out_path.
        // Returns Status::failure on error; repeated calls reuse cached outputs on success.
        Status render_svg(const std::string &latex, std::filesystem::path &out_path);

        const std::string &microtex_binary() const { return microtex_bin_; }
        bool available() const { return available_; }
        const std::string &last_error() const { return last_error_; }

    private:
        std::filesystem::path scratch_dir_;
        std::string microtex_bin_;
        bool available_ = false;
        std::string last_error_;

        Status locate_microtex(std::string &out_path) const;
    };

} // namespace panim
