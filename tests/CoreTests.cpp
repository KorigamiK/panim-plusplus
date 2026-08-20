#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

#include "panim/Frame.hpp"
#include "panim/Painter.hpp"
#include "panim/RenderSession.hpp"
#include "panim/SceneSequence.hpp"
#include "panim/Timeline.hpp"

namespace {

    int failures = 0;

    void check(bool condition, std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }

    bool near(double value, double expected, double tolerance = 0.001) { return std::abs(value - expected) <= tolerance; }

    void test_timeline() {
        panim::anim::Track<double> track;
        track.add(0.0, 0.0, panim::anim::ease_linear);
        track.add(2.0, 10.0, panim::anim::ease_linear);
        check(near(track.sample(-1.0), 0.0), "track clamps before its first key");
        check(near(track.sample(1.0), 5.0), "track linearly interpolates");
        check(near(track.sample(4.0), 10.0), "track clamps after its last key");
        check(near(track.sample_loop(3.0), 5.0), "track loops predictably");
    }

    void test_frame_and_painter() {
        panim::Frame empty(-2, 8);
        check(empty.width == 0 && empty.pixels.empty(), "negative frame dimensions produce an empty frame");

        panim::Frame frame(2, 2);
        panim::Painter painter(frame);
        painter.clear({10, 20, 30, 40});
        const uint8_t *pixel = frame.pixel_ptr(0, 0);
        check(pixel[0] == 10 && pixel[1] == 20 && pixel[2] == 30 && pixel[3] == 40, "Painter::clear replaces RGBA instead of blending");

        panim::Frame source(1, 1);
        source.clear(200, 100, 50, 255);
        painter.clear({0, 0, 0, 255});
        painter.blit_scaled(source, 0, 0, 2, 2);
        pixel = frame.pixel_ptr(1, 1);
        check(pixel[0] == 200 && pixel[1] == 100 && pixel[2] == 50, "scaled blit covers its destination");
    }

    void test_scene_sequence() {
        panim::SceneSequence scenes;
        scenes.add("red", 1.0, [](panim::Frame &frame, const panim::SceneTime &) { frame.clear(255, 0, 0, 255); }, 0.0);
        scenes.add("blue", 1.0, [](panim::Frame &frame, const panim::SceneTime &) { frame.clear(0, 0, 255, 255); }, 0.5);

        check(near(scenes.duration(), 2.0), "scene durations compose");
        panim::SceneSample sample = scenes.sample(1.25);
        check(sample.valid && sample.index == 1 && sample.name == "blue", "scene sample identifies the active scene");
        check(near(sample.time.local_seconds, 0.25) && near(sample.transition_progress, 0.5), "scene sample exposes local and transition time");

        panim::Frame frame(2, 2);
        scenes.render(frame, 1.25);
        const uint8_t *pixel = frame.pixel_ptr(0, 0);
        check(pixel[0] >= 127 && pixel[0] <= 128 && pixel[2] >= 127 && pixel[2] <= 128, "scene transition crossfades rendered frames");
        scenes.render(frame, 1.75);
        pixel = frame.pixel_ptr(0, 0);
        check(pixel[0] == 0 && pixel[2] == 255, "scene renders directly after its transition");
    }

    class SessionAnimation final : public panim::Animation {
    public:
        void on_setup(const panim::AnimationContext &context) override {
            setup_width = context.width;
            setup_height = context.height;
        }

        void render_frame(panim::Frame &frame, double time_seconds) override {
            rendered_times.push_back(time_seconds);
            frame.set_pixel(0, 0, static_cast<uint8_t>(time_seconds * 10.0), 0, 0);
        }

        int setup_width = 0;
        int setup_height = 0;
        std::vector<double> rendered_times;
    };

    class RecordingSink final : public panim::FrameSink {
    public:
        panim::Status submit(const panim::Frame &frame, int frame_index, double time_seconds) override {
            indices.push_back(frame_index);
            times.push_back(time_seconds);
            first_channels.push_back(frame.pixels[0]);
            return panim::Status::success();
        }

        panim::Status finish() override {
            finished = true;
            return panim::Status::success();
        }

        std::vector<int> indices;
        std::vector<double> times;
        std::vector<uint8_t> first_channels;
        bool finished = false;
    };

    void test_render_session() {
        SessionAnimation animation;
        panim::RenderSessionOptions options;
        options.width = 8;
        options.height = 6;
        options.fps = 4.0;
        options.duration = 2.0;
        panim::RenderSession session(animation, options);
        panim::Status status = session.setup();
        check(status.ok, "render session accepts valid settings");
        check(animation.setup_width == 8 && animation.setup_height == 6, "render session supplies dimensions to animation setup");

        RecordingSink sink;
        status = session.render_frames(sink, 0.5, 3);
        check(status.ok && sink.finished, "render session explicitly finishes its frame sink");
        check(sink.indices.size() == 3 && sink.indices[2] == 2, "render session submits requested frame indices");
        check(sink.times.size() == 3 && near(sink.times[0], 0.5) && near(sink.times[1], 0.75) && near(sink.times[2], 1.0),
              "render session samples the timeline at the configured fps");
        check(sink.first_channels.size() == 3 && sink.first_channels[0] == 5 && sink.first_channels[2] == 10,
              "frame sinks observe the animation's rendered RGBA pixels");
    }

} // namespace

int main() {
    test_timeline();
    test_frame_and_painter();
    test_scene_sequence();
    test_render_session();
    if (failures == 0)
        std::cout << "All panim core tests passed\n";
    return failures == 0 ? 0 : 1;
}
