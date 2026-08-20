#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Preview.hpp"
#include "panim/Animation.hpp"
#include "panim/Frame.hpp"
#include "panim/FrameSink.hpp"
#include "panim/LatexRenderer.hpp"
#include "panim/Log.hpp"
#include "panim/PluginHost.hpp"
#include "panim/RenderSession.hpp"
#include "panim/Status.hpp"

namespace {

#ifndef PANIM_PLUGIN_PREFIX
#define PANIM_PLUGIN_PREFIX "lib"
#endif

#ifndef PANIM_PLUGIN_SUFFIX
#define PANIM_PLUGIN_SUFFIX ".so"
#endif

    enum class Command {
        Render,
        Frame,
        Preview,
    };

    struct Args {
        Command command = Command::Render;
        std::string plugin;
        std::optional<double> duration;
        std::optional<int> width;
        std::optional<int> height;
        std::optional<double> fps;
        std::optional<std::filesystem::path> output;
        std::optional<int> frame_limit;
        panim::VideoProfile quality = panim::VideoProfile::Share;
        int supersample = 1;
        double start_time = 0.0;
        bool watch_plugin = true;
        bool show_help = false;
        panim::Status status = panim::Status::success();
    };

    std::string plugin_filename(std::string_view name) { return std::string(PANIM_PLUGIN_PREFIX) + std::string(name) + PANIM_PLUGIN_SUFFIX; }

    std::filesystem::path executable_path(const char *argv0) {
        std::error_code error;
        std::filesystem::path path = std::filesystem::canonical(argv0, error);
        if (!error)
            return path;
        error.clear();
        path = std::filesystem::absolute(argv0, error);
        return error ? std::filesystem::path(argv0) : path;
    }

    std::vector<std::filesystem::path> plugin_directories(const std::filesystem::path &executable) {
        const std::filesystem::path executable_dir = executable.parent_path();
        return {
            executable_dir.parent_path() / "plugins",
            executable_dir / "plugins",
            executable_dir.parent_path() / "lib/panim/plugins",
            executable_dir.parent_path() / "lib64/panim/plugins",
        };
    }

    bool path_exists(const std::filesystem::path &path) {
        std::error_code error;
        bool exists = std::filesystem::exists(path, error);
        return exists && !error;
    }

    std::filesystem::path resolve_plugin(std::string_view requested, const std::filesystem::path &executable) {
        const auto directories = plugin_directories(executable);
        if (requested.empty()) {
            for (std::string_view name : {"FeatureTour", "Showcase", "SampleWave"}) {
                for (const auto &directory : directories) {
                    std::filesystem::path candidate = directory / plugin_filename(name);
                    if (path_exists(candidate))
                        return candidate;
                }
            }
            return directories.front() / plugin_filename("SampleWave");
        }

        std::filesystem::path direct(requested);
        if (path_exists(direct) || direct.has_parent_path() || direct.has_extension())
            return direct;

        for (const auto &directory : directories) {
            std::filesystem::path candidate = directory / plugin_filename(requested);
            if (path_exists(candidate))
                return candidate;
        }
        return direct;
    }

    bool parse_double(std::string_view text, double &value) {
        std::string input(text);
        char *end = nullptr;
        errno = 0;
        double parsed = std::strtod(input.c_str(), &end);
        if (errno == ERANGE || end != input.c_str() + input.size() || !std::isfinite(parsed)) {
            return false;
        }
        value = parsed;
        return true;
    }

    bool parse_int(std::string_view text, int &value) {
        std::string input(text);
        char *end = nullptr;
        errno = 0;
        long parsed = std::strtol(input.c_str(), &end, 10);
        if (errno == ERANGE || end != input.c_str() + input.size() || parsed < std::numeric_limits<int>::min() ||
            parsed > std::numeric_limits<int>::max()) {
            return false;
        }
        value = static_cast<int>(parsed);
        return true;
    }

    bool parse_size(std::string_view text, int &width, int &height) {
        size_t separator = text.find_first_of("xX");
        if (separator == std::string_view::npos)
            return false;
        return parse_int(text.substr(0, separator), width) && parse_int(text.substr(separator + 1), height);
    }

    bool parse_quality(std::string_view text, panim::VideoProfile &quality) {
        if (text == "draft") {
            quality = panim::VideoProfile::Draft;
            return true;
        }
        if (text == "share") {
            quality = panim::VideoProfile::Share;
            return true;
        }
        if (text == "master") {
            quality = panim::VideoProfile::Master;
            return true;
        }
        return false;
    }

    bool is_png_output(const std::filesystem::path &path) {
        std::string extension = path.extension().string();
        for (char &character : extension) {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        return extension == ".png";
    }

    panim::Status missing_value(std::string_view option) { return panim::Status::failure("Missing value after " + std::string(option)); }

    Args parse_args(int argc, char **argv) {
        Args args;
        std::vector<std::string_view> positional;

        for (int index = 1; index < argc; ++index) {
            std::string_view argument(argv[index]);
            if (argument == "--help" || argument == "-h") {
                args.show_help = true;
                continue;
            }

            auto next_value = [&]() -> std::optional<std::string_view> {
                if (index + 1 >= argc)
                    return std::nullopt;
                ++index;
                return std::string_view(argv[index]);
            };

            if (argument == "--plugin" || argument == "-p") {
                auto value = next_value();
                if (!value) {
                    args.status = missing_value(argument);
                    return args;
                }
                args.plugin = *value;
            } else if (argument == "--duration" || argument == "-d") {
                auto value = next_value();
                double parsed = 0.0;
                if (!value || !parse_double(*value, parsed)) {
                    args.status = value ? panim::Status::failure("Invalid duration: " + std::string(*value)) : missing_value(argument);
                    return args;
                }
                args.duration = parsed;
            } else if (argument == "--size") {
                auto value = next_value();
                int width = 0;
                int height = 0;
                if (!value || !parse_size(*value, width, height)) {
                    args.status = value ? panim::Status::failure("Invalid size; expected WIDTHxHEIGHT") : missing_value(argument);
                    return args;
                }
                args.width = width;
                args.height = height;
            } else if (argument == "--width") {
                auto value = next_value();
                int parsed = 0;
                if (!value || !parse_int(*value, parsed)) {
                    args.status = value ? panim::Status::failure("Invalid width: " + std::string(*value)) : missing_value(argument);
                    return args;
                }
                args.width = parsed;
            } else if (argument == "--height") {
                auto value = next_value();
                int parsed = 0;
                if (!value || !parse_int(*value, parsed)) {
                    args.status = value ? panim::Status::failure("Invalid height: " + std::string(*value)) : missing_value(argument);
                    return args;
                }
                args.height = parsed;
            } else if (argument == "--fps") {
                auto value = next_value();
                double parsed = 0.0;
                if (!value || !parse_double(*value, parsed)) {
                    args.status = value ? panim::Status::failure("Invalid fps: " + std::string(*value)) : missing_value(argument);
                    return args;
                }
                args.fps = parsed;
            } else if (argument == "--output" || argument == "-o") {
                auto value = next_value();
                if (!value) {
                    args.status = missing_value(argument);
                    return args;
                }
                args.output = std::filesystem::path(*value);
            } else if (argument == "--start" || argument == "--from" || argument == "--time") {
                auto value = next_value();
                if (!value || !parse_double(*value, args.start_time)) {
                    args.status = value ? panim::Status::failure("Invalid start time: " + std::string(*value)) : missing_value(argument);
                    return args;
                }
            } else if (argument == "--frames") {
                auto value = next_value();
                int parsed = 0;
                if (!value || !parse_int(*value, parsed)) {
                    args.status = value ? panim::Status::failure("Invalid frame count: " + std::string(*value)) : missing_value(argument);
                    return args;
                }
                args.frame_limit = parsed;
            } else if (argument == "--quality") {
                auto value = next_value();
                if (!value || !parse_quality(*value, args.quality)) {
                    args.status = value ? panim::Status::failure("Invalid quality; expected "
                                                                 "draft, share, or master")
                                        : missing_value(argument);
                    return args;
                }
            } else if (argument == "--supersample") {
                auto value = next_value();
                int parsed = 0;
                if (!value || !parse_int(*value, parsed)) {
                    args.status = value ? panim::Status::failure("Invalid supersample factor: " + std::string(*value)) : missing_value(argument);
                    return args;
                }
                args.supersample = parsed;
            } else if (argument == "--watch") {
                args.watch_plugin = true;
            } else if (argument == "--no-watch") {
                args.watch_plugin = false;
            } else if (!argument.empty() && argument.front() == '-') {
                args.status = panim::Status::failure("Unknown option: " + std::string(argument));
                return args;
            } else {
                positional.push_back(argument);
            }
        }

        if (!positional.empty()) {
            if (positional.front() == "render") {
                args.command = Command::Render;
                positional.erase(positional.begin());
            } else if (positional.front() == "frame") {
                args.command = Command::Frame;
                positional.erase(positional.begin());
            } else if (positional.front() == "preview") {
                args.command = Command::Preview;
                positional.erase(positional.begin());
            }
        }

        if (positional.size() > 6) {
            args.status = panim::Status::failure("Too many positional arguments");
            return args;
        }
        if (!positional.empty()) {
            if (!args.plugin.empty()) {
                args.status = panim::Status::failure("Plugin was provided twice");
                return args;
            }
            args.plugin = positional[0];
        }

        auto parse_positional_double = [&](size_t position, std::optional<double> &target, const char *label) {
            if (positional.size() <= position || target)
                return true;
            double value = 0.0;
            if (!parse_double(positional[position], value)) {
                args.status = panim::Status::failure(std::string("Invalid ") + label + ": " + std::string(positional[position]));
                return false;
            }
            target = value;
            return true;
        };
        auto parse_positional_int = [&](size_t position, std::optional<int> &target, const char *label) {
            if (positional.size() <= position || target)
                return true;
            int value = 0;
            if (!parse_int(positional[position], value)) {
                args.status = panim::Status::failure(std::string("Invalid ") + label + ": " + std::string(positional[position]));
                return false;
            }
            target = value;
            return true;
        };

        if (!parse_positional_double(1, args.duration, "duration") || !parse_positional_int(2, args.width, "width") ||
            !parse_positional_int(3, args.height, "height") || !parse_positional_double(4, args.fps, "fps")) {
            return args;
        }
        if (positional.size() > 5 && !args.output)
            args.output = std::filesystem::path(positional[5]);
        return args;
    }

    void print_usage() {
        std::cout << "panim++ — preview and render C++ animation plugins\n\n"
                  << "Usage:\n"
                  << "  panim preview [plugin] [options]\n"
                  << "  panim frame [plugin] [options]\n"
                  << "  panim render [plugin] [options]\n"
                  << "  panim [plugin] [duration] [width] [height] [fps] [output]\n"
                  << "  panim [options]  (backward-compatible render mode)\n\n"
                  << "Options:\n"
                  << "  -p, --plugin NAME|PATH  Plugin name or shared-library path\n"
                  << "  -d, --duration SECONDS  Output duration (plugin default otherwise)\n"
                  << "      --size WIDTHxHEIGHT Output dimensions\n"
                  << "      --fps RATE          Frames per second\n"
                  << "      --start, --time S   Start/seek on the animation timeline\n"
                  << "      --frames COUNT      Limit render or auto-close preview\n"
                  << "      --quality PROFILE   draft, share (default), or master\n"
                  << "      --supersample N      Render at Nx, then downsample (1-4)\n"
                  << "      --[no-]watch        Toggle preview plugin hot reload\n"
                  << "  -o, --output PATH       Video/PNG path; preview screenshot dir\n"
                  << "  -h, --help              Show this help\n\n"
                  << "Examples:\n"
                  << "  panim preview FeatureTour\n"
                  << "  panim frame Showcase --time 2.0 --output still.png\n"
                  << "  panim render FeatureTour --size 3840x2160 --quality master\n\n"
                  << "Preview controls:\n"
                  << "  Click restart, step, play/pause, timeline, PNG, or reload;\n"
                  << "  Space play/pause, Left/Right step, Shift step 1s,\n"
                  << "  Home/End seek, S screenshot, R reload, Esc quit\n";
    }

    std::string output_stem(const char *name) {
        std::string stem = name ? name : "animation";
        for (char &character : stem) {
            bool valid = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') ||
                         character == '-' || character == '_';
            if (!valid)
                character = '-';
        }
        return stem.empty() ? "animation" : stem;
    }

} // namespace

int main(int argc, char **argv) {
    panim::init_logging();
    Args args = parse_args(argc, argv);
    if (args.show_help) {
        print_usage();
        return 0;
    }
    if (!args.status.ok) {
        PANIM_LOG_ERROR("{}", args.status.message);
        print_usage();
        return 2;
    }

    std::filesystem::path executable = executable_path(argv[0]);
    std::filesystem::path plugin_path = resolve_plugin(args.plugin, executable);

    if (args.command == Command::Preview) {
        if (args.start_time < 0.0 || (args.width && *args.width <= 0) || (args.height && *args.height <= 0) || (args.fps && *args.fps <= 0.0) ||
            (args.duration && *args.duration <= 0.0) || (args.frame_limit && *args.frame_limit <= 0)) {
            PANIM_LOG_ERROR("Invalid preview settings");
            return 2;
        }
        if (args.supersample != 1) {
            PANIM_LOG_ERROR("--supersample applies to final render; use --size to set "
                            "preview resolution");
            return 2;
        }
        panim::PreviewOptions preview_options;
        preview_options.plugin_path = plugin_path;
        preview_options.output_dir = args.output.value_or("panim_out");
        preview_options.width = args.width;
        preview_options.height = args.height;
        preview_options.fps = args.fps;
        preview_options.duration = args.duration;
        preview_options.frame_limit = args.frame_limit;
        preview_options.start_time = args.start_time;
        preview_options.watch_plugin = args.watch_plugin;
        panim::Status preview_status = panim::run_preview(preview_options);
        if (!preview_status.ok) {
            PANIM_LOG_ERROR("Preview failed: {}", preview_status.message);
            return 1;
        }
        return 0;
    }

    panim::PluginHost host(plugin_path);
    if (!host.valid()) {
        PANIM_LOG_ERROR("Plugin load failed: {}", host.status().message);
        return 1;
    }

    auto animation = host.create();
    if (!animation) {
        PANIM_LOG_ERROR("Failed to create animation instance.");
        return 1;
    }

    panim::AnimationInfo info = animation->info();
    double fps = args.fps.value_or(info.fps);
    double start_time = args.start_time;
    double default_duration = std::max(1.0 / std::max(fps, 1.0), info.duration - start_time);
    double duration = args.duration.value_or(default_duration);
    int width = args.width.value_or(info.width);
    int height = args.height.value_or(info.height);
    const bool frame_command = args.command == Command::Frame;
    std::filesystem::path output =
        args.output.value_or(std::filesystem::path("panim_out") / (output_stem(info.name) + (frame_command ? ".png" : ".mp4")));
    const bool still_output = frame_command || is_png_output(output);

    if (frame_command && !is_png_output(output)) {
        PANIM_LOG_ERROR("The frame command requires a .png output path");
        return 2;
    }

    if (duration <= 0.0 || fps <= 0.0 || start_time < 0.0 || width <= 0 || height <= 0 || (!still_output && (width % 2 != 0 || height % 2 != 0)) ||
        (args.frame_limit && *args.frame_limit <= 0) || args.supersample < 1 || args.supersample > 4) {
        PANIM_LOG_ERROR("Invalid render settings: duration/fps/size must be positive, "
                        "video dimensions must be even, supersample must be 1-4, and "
                        "start must be non-negative");
        return 2;
    }

    long long requested_frames = still_output ? 1 : args.frame_limit ? *args.frame_limit : std::llround(duration * fps);
    if (requested_frames <= 0 || requested_frames > std::numeric_limits<int>::max()) {
        PANIM_LOG_ERROR("Requested frame count is out of range: {}", requested_frames);
        return 2;
    }
    int frame_count = static_cast<int>(requested_frames);
    long long render_width_long = static_cast<long long>(width) * args.supersample;
    long long render_height_long = static_cast<long long>(height) * args.supersample;
    if (render_width_long > std::numeric_limits<int>::max() || render_height_long > std::numeric_limits<int>::max()) {
        PANIM_LOG_ERROR("Supersampled render dimensions are out of range");
        return 2;
    }
    int render_width = static_cast<int>(render_width_long);
    int render_height = static_cast<int>(render_height_long);

    std::filesystem::path output_dir = output.has_parent_path() ? output.parent_path() : std::filesystem::path(".");
    std::error_code directory_error;
    std::filesystem::create_directories(output_dir, directory_error);
    if (directory_error) {
        PANIM_LOG_ERROR("Failed to create output directory {}: {}", output_dir.string(), directory_error.message());
        return 1;
    }

    PANIM_LOG_INFO("Animation: {}", info.name ? info.name : "Untitled");
    PANIM_LOG_INFO("Using plugin: {}", plugin_path.string());
    PANIM_LOG_INFO("Output: {} ({}x{} @ {} fps, {} frames from {} s)", output.string(), width, height, fps, frame_count, start_time);
    if (args.supersample > 1) {
        PANIM_LOG_INFO("Supersampling: rendering {}x{} and downsampling to {}x{}", render_width, render_height, width, height);
    }

    panim::LatexRenderer latex(output_dir / "latex");
    if (!latex.available())
        PANIM_LOG_WARN("LaTeX disabled: {}", latex.last_error());

    panim::RenderSessionOptions session_options;
    session_options.width = render_width;
    session_options.height = render_height;
    session_options.fps = fps;
    session_options.duration = duration;
    session_options.latex = latex.available() ? &latex : nullptr;
    session_options.output_dir = output_dir;
    panim::RenderSession session(*animation, session_options);
    panim::Status setup_status = session.setup();
    if (!setup_status.ok) {
        PANIM_LOG_ERROR("Animation setup failed: {}", setup_status.message);
        return 1;
    }

    panim::VideoWriterOptions writer_options;
    writer_options.profile = args.quality;
    writer_options.input_width = render_width;
    writer_options.input_height = render_height;
    panim::VideoFrameSink sink(output, width, height, fps, writer_options);
    if (!sink.ok()) {
        PANIM_LOG_ERROR("Video writer initialization failed: {}", sink.status().message);
        return 1;
    }

    panim::Status render_status = session.render_frames(sink, start_time, frame_count);
    if (!render_status.ok) {
        PANIM_LOG_ERROR("Rendering failed: {}", render_status.message);
        return 1;
    }
    PANIM_LOG_INFO("Rendered {} to {}", still_output ? "image" : "video", output.string());

    return 0;
}
