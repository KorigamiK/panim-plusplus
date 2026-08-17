// Reusable animation setup and timeline rendering session.
#pragma once

#include <filesystem>

#include "Animation.hpp"
#include "Frame.hpp"
#include "FrameSink.hpp"
#include "Status.hpp"

namespace panim {

    struct RenderSessionOptions {
        int width = 1280;
        int height = 720;
        double fps = 30.0;
        double duration = 1.0;
        LatexRenderer *latex = nullptr;
        std::filesystem::path output_dir;
    };

    class RenderSession {
    public:
        RenderSession(Animation &animation,
                      const RenderSessionOptions &options);

        Status setup();
        Status render_at(double time_seconds);
        Status render_frames(FrameSink &sink,
                             double start_time,
                             int frame_count);

        const Frame &frame() const { return frame_; }
        Frame &frame() { return frame_; }
        const RenderSessionOptions &options() const { return options_; }

    private:
        Animation &animation_;
        RenderSessionOptions options_;
        Frame frame_;
        bool setup_complete_ = false;
    };

} // namespace panim
