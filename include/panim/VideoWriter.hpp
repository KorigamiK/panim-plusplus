// FFmpeg-backed video writer for RGBA frames.
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "Frame.hpp"
#include "Status.hpp"

namespace panim {

    class VideoWriter {
    public:
        VideoWriter(const std::filesystem::path &path,
                    int width,
                    int height,
                    double fps,
                    int bitrate = 2'000'000);
        ~VideoWriter();

        VideoWriter(const VideoWriter &) = delete;
        VideoWriter &operator=(const VideoWriter &) = delete;

        Status write_frame(const Frame &frame);
        const Status &status() const { return status_; }
        bool ok() const { return status_.ok; }

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        Status status_ = Status::success();
    };

} // namespace panim
