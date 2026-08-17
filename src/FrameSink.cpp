#include "panim/FrameSink.hpp"

namespace panim {

    VideoFrameSink::VideoFrameSink(const std::filesystem::path &path,
                                   int output_width,
                                   int output_height,
                                   double fps,
                                   const VideoWriterOptions &options)
        : writer_(std::make_unique<VideoWriter>(path,
                                                output_width,
                                                output_height,
                                                fps,
                                                options)) {
        status_ = writer_->status();
    }

    VideoFrameSink::~VideoFrameSink() = default;

    Status VideoFrameSink::submit(const Frame &frame,
                                  int,
                                  double) {
        if (!status_.ok)
            return status_;
        status_ = writer_->write_frame(frame);
        return status_;
    }

    Status VideoFrameSink::finish() {
        if (!writer_)
            return status_;
        status_ = writer_->finish();
        return status_;
    }

} // namespace panim
