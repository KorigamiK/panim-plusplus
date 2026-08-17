// Output boundary for rendered RGBA frames.
#pragma once

#include <filesystem>
#include <memory>

#include "Frame.hpp"
#include "Status.hpp"
#include "VideoWriter.hpp"

namespace panim {

    class FrameSink {
    public:
        virtual ~FrameSink() = default;

        virtual Status submit(const Frame &frame,
                              int frame_index,
                              double time_seconds) = 0;
        virtual Status finish() = 0;
    };

    class VideoFrameSink final : public FrameSink {
    public:
        VideoFrameSink(const std::filesystem::path &path,
                       int output_width,
                       int output_height,
                       double fps,
                       const VideoWriterOptions &options = {});
        ~VideoFrameSink() override;

        VideoFrameSink(const VideoFrameSink &) = delete;
        VideoFrameSink &operator=(const VideoFrameSink &) = delete;

        Status submit(const Frame &frame,
                      int frame_index,
                      double time_seconds) override;
        Status finish() override;
        const Status &status() const { return status_; }
        bool ok() const { return status_.ok; }

    private:
        std::unique_ptr<VideoWriter> writer_;
        Status status_ = Status::success();
    };

} // namespace panim
