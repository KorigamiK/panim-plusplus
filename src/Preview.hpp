#pragma once

#include <filesystem>
#include <optional>

#include "panim/Status.hpp"

namespace panim {

    struct PreviewOptions {
        std::filesystem::path plugin_path;
        std::filesystem::path output_dir = "panim_out";
        std::optional<int> width;
        std::optional<int> height;
        std::optional<double> fps;
        std::optional<double> duration;
        std::optional<int> frame_limit;
        double start_time = 0.0;
        bool watch_plugin = true;
    };

    Status run_preview(const PreviewOptions &options);

} // namespace panim
