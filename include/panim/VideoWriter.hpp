// FFmpeg-backed video writer for RGBA frames.
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "Frame.hpp"
#include "Status.hpp"

namespace panim {

    enum class VideoProfile {
        Draft,
        Share,
        Master,
    };

    struct VideoWriterOptions {
        VideoProfile profile = VideoProfile::Share;
        // A non-positive bitrate selects the profile's quality-based mode.
        int bitrate = 0;
        // Input dimensions may be larger than the encoded image to enable
        // whole-frame supersampling. Zero selects the output dimension.
        int input_width = 0;
        int input_height = 0;
    };

    class VideoWriter {
    public:
        // A non-positive bitrate selects quality-based H.264 encoding.
        VideoWriter(const std::filesystem::path &path,
                    int width,
                    int height,
                    double fps,
                    int bitrate = 0);
        VideoWriter(const std::filesystem::path &path,
                    int width,
                    int height,
                    double fps,
                    const VideoWriterOptions &options);
        ~VideoWriter();

        VideoWriter(const VideoWriter &) = delete;
        VideoWriter &operator=(const VideoWriter &) = delete;

        Status write_frame(const Frame &frame);
        Status finish();
        const Status &status() const { return status_; }
        bool ok() const { return status_.ok; }

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        Status status_ = Status::success();
    };

} // namespace panim
