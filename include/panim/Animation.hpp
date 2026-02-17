// Abstract animation contract that plugins implement.
#pragma once

#include <filesystem>

#include "Frame.hpp"

namespace panim {

    class LatexRenderer;

    struct AnimationContext {
        int width = 1920;
        int height = 1080;
        double fps = 30.0;
        double duration = 1.0;          // seconds
        LatexRenderer *latex = nullptr; // Optional helper shared with plugins
        std::filesystem::path output_dir;
    };

    class Animation {
    public:
        virtual ~Animation() = default;
        virtual void on_setup(const AnimationContext &ctx) = 0;
        virtual void render_frame(Frame &frame, double time_sec) = 0;
    };

    using CreateAnimationFn = Animation *(*)();
    using DestroyAnimationFn = void (*)(Animation *);

} // namespace panim

// Plugin entry points (C ABI) discovered via dlsym.
extern "C" {
panim::Animation *create_animation();
void destroy_animation(panim::Animation *);
}
