#include "panim/RenderSession.hpp"

#include <cmath>

namespace panim {

    RenderSession::RenderSession(Animation &animation,
                                 const RenderSessionOptions &options)
        : animation_(animation),
          options_(options),
          frame_(options.width, options.height) {}

    Status RenderSession::setup() {
        if (options_.width <= 0 || options_.height <= 0 ||
            options_.fps <= 0.0 || options_.duration <= 0.0 ||
            !std::isfinite(options_.fps) ||
            !std::isfinite(options_.duration)) {
            return Status::failure("Invalid render session settings");
        }

        AnimationContext context;
        context.width = options_.width;
        context.height = options_.height;
        context.fps = options_.fps;
        context.duration = options_.duration;
        context.latex = options_.latex;
        context.output_dir = options_.output_dir;
        animation_.on_setup(context);
        setup_complete_ = true;
        return Status::success();
    }

    Status RenderSession::render_at(double time_seconds) {
        if (!setup_complete_)
            return Status::failure("Render session has not been set up");
        if (!std::isfinite(time_seconds) || time_seconds < 0.0)
            return Status::failure("Invalid animation time");

        frame_.clear(8, 12, 20, 255);
        animation_.render_frame(frame_, time_seconds);
        return Status::success();
    }

    Status RenderSession::render_frames(FrameSink &sink,
                                        double start_time,
                                        int frame_count) {
        if (frame_count <= 0)
            return Status::failure("Frame count must be positive");

        const double time_step = 1.0 / options_.fps;
        for (int index = 0; index < frame_count; ++index) {
            double time_seconds = start_time + index * time_step;
            Status status = render_at(time_seconds);
            if (!status.ok)
                return status;
            status = sink.submit(frame_, index, time_seconds);
            if (!status.ok)
                return status;
        }
        return sink.finish();
    }

} // namespace panim
