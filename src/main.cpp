#include <filesystem>
#include <string>
#include <vector>

#include "panim/Animation.hpp"
#include "panim/Frame.hpp"
#include "panim/LatexRenderer.hpp"
#include "panim/Log.hpp"
#include "panim/PluginHost.hpp"
#include "panim/VideoWriter.hpp"

using panim::AnimationContext;

namespace {

    struct Args {
        std::filesystem::path plugin_path;
        double duration = 2.0;
        int width = 1280;
        int height = 720;
        double fps = 30.0;
        std::filesystem::path output = "panim_out/output.mp4";
    };

    std::filesystem::path default_plugin(const std::filesystem::path &exe_path) {
        auto exe_dir = exe_path.parent_path();
        std::vector<std::filesystem::path> candidates{
            exe_dir.parent_path() / "plugins/libShowcase.so",
            exe_dir.parent_path() / "plugins/libSampleWave.so",
            exe_dir / "plugins/libShowcase.so",
            exe_dir / "plugins/libSampleWave.so"};
        for (const auto &c : candidates) {
            if (std::filesystem::exists(c))
                return c;
        }
        // Final fallback keeps previous default path.
        return exe_dir.parent_path() / "plugins/libSampleWave.so";
    }

    Args parse_args(int argc, char **argv) {
        Args opts;
        std::error_code ec;
        auto exe = std::filesystem::canonical(argv[0], ec);
        if (ec) {
            exe = std::filesystem::absolute(argv[0]);
        }
        if (argc > 1) {
            opts.plugin_path = argv[1];
        } else {
            opts.plugin_path = default_plugin(exe);
        }
        if (argc > 2)
            opts.duration = std::stod(argv[2]);
        if (argc > 3)
            opts.width = std::stoi(argv[3]);
        if (argc > 4)
            opts.height = std::stoi(argv[4]);
        if (argc > 5)
            opts.fps = std::stod(argv[5]);
        if (argc > 6)
            opts.output = argv[6];
        return opts;
    }

} // namespace

int main(int argc, char **argv) {
    panim::init_logging();
    auto args = parse_args(argc, argv);

    PANIM_LOG_INFO("Using plugin: {}", args.plugin_path.string());
    PANIM_LOG_INFO("Output: {} ({}x{} @ {} fps, {} s)", args.output.string(), args.width, args.height, args.fps, args.duration);

    panim::LatexRenderer latex("panim_out/latex");
    if (!latex.available()) {
        PANIM_LOG_WARN("LaTeX disabled: {}", latex.last_error());
    }

    AnimationContext ctx;
    ctx.width = args.width;
    ctx.height = args.height;
    ctx.fps = args.fps;
    ctx.duration = args.duration;
    ctx.latex = &latex;
    ctx.output_dir = std::filesystem::path("panim_out");

    panim::PluginHost host(args.plugin_path);
    if (!host.valid()) {
        PANIM_LOG_ERROR("Plugin load failed: {}", host.status().message);
        return 1;
    }

    auto animation = host.create();
    if (!animation) {
        PANIM_LOG_ERROR("Failed to create animation instance.");
        return 1;
    }
    animation->on_setup(ctx);

    int frame_count = static_cast<int>(ctx.duration * ctx.fps);
    double dt = 1.0 / ctx.fps;
    auto parent = args.output.has_parent_path() ? args.output.parent_path() : std::filesystem::path(".");
    std::filesystem::create_directories(parent);

#ifdef PANIM_ENABLE_FFMPEG
    panim::VideoWriter writer(args.output, ctx.width, ctx.height, ctx.fps);
    if (!writer.ok()) {
        PANIM_LOG_ERROR("Video writer initialization failed: {}", writer.status().message);
        return 1;
    }

    for (int i = 0; i < frame_count; ++i) {
        double t = i * dt;
        panim::Frame frame(ctx.width, ctx.height);
        frame.clear(8, 12, 20, 255);
        animation->render_frame(frame, t);
        auto st = writer.write_frame(frame);
        if (!st.ok) {
            PANIM_LOG_ERROR("Encoding failed at frame {}: {}", i, st.message);
            return 1;
        }
    }
    PANIM_LOG_INFO("Rendered video to {}", args.output.string());
#else
    (void)frame_count;
    PANIM_LOG_ERROR("FFmpeg disabled at build time; rebuild with -DPANIM_ENABLE_FFMPEG=ON to emit video.");
    return 1;
#endif

    return 0;
}
