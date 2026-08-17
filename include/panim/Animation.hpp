// Abstract animation contract that plugins implement.
#pragma once

#include <cstdint>
#include <filesystem>

#include "Frame.hpp"

namespace panim {

    inline constexpr uint32_t plugin_api_version = 1;

    class LatexRenderer;

    struct AnimationInfo {
        const char *name = "Untitled";
        double duration = 4.0;
        int width = 1280;
        int height = 720;
        double fps = 30.0;
    };

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
        virtual AnimationInfo info() const { return {}; }
        virtual void on_setup(const AnimationContext &ctx) = 0;
        virtual void render_frame(Frame &frame, double time_sec) = 0;
    };

    using CreateAnimationFn = Animation *(*)();
    using DestroyAnimationFn = void (*)(Animation *);
    using PluginApiVersionFn = uint32_t (*)();

} // namespace panim

// Plugin entry points (C ABI) discovered via dlsym.
extern "C" {
panim::Animation *create_animation();
void destroy_animation(panim::Animation *);
uint32_t panim_plugin_api_version();
}
