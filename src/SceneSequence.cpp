#include "panim/SceneSequence.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace panim {

    namespace {

        double smootherstep(double value) {
            double t = std::clamp(value, 0.0, 1.0);
            return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
        }

        uint8_t blend_channel(uint8_t a, uint8_t b, double amount) {
            return static_cast<uint8_t>(std::lround(
                static_cast<double>(a) +
                (static_cast<double>(b) - static_cast<double>(a)) * amount));
        }

        void blend_frames(const Frame &from,
                          const Frame &to,
                          Frame &target,
                          double amount) {
            double eased = smootherstep(amount);
            const size_t byte_count = target.pixels.size();
            for (size_t i = 0; i < byte_count; ++i) {
                target.pixels[i] = blend_channel(
                    from.pixels[i], to.pixels[i], eased);
            }
        }

    } // namespace

    SceneSequence &SceneSequence::add(std::string name,
                                      double duration,
                                      RenderFn render,
                                      double transition) {
        if (duration <= 0.0 || !render)
            return *this;

        double safe_transition = scenes_.empty()
                                     ? 0.0
                                     : std::clamp(transition, 0.0, duration);
        scenes_.push_back({std::move(name),
                           total_duration_,
                           duration,
                           safe_transition,
                           std::move(render)});
        total_duration_ += duration;
        return *this;
    }

    SceneSample SceneSequence::sample(double time_seconds) const {
        if (scenes_.empty() || total_duration_ <= 0.0)
            return {};

        double timeline_time = time_seconds;
        if (looping_) {
            timeline_time = std::fmod(timeline_time, total_duration_);
            if (timeline_time < 0.0)
                timeline_time += total_duration_;
        } else {
            timeline_time = std::clamp(timeline_time, 0.0, total_duration_);
        }

        size_t index = scenes_.size() - 1;
        for (size_t i = 0; i < scenes_.size(); ++i) {
            const Scene &scene = scenes_[i];
            if (timeline_time < scene.start + scene.duration ||
                i + 1 == scenes_.size()) {
                index = i;
                break;
            }
        }

        const Scene &scene = scenes_[index];
        double local = std::clamp(
            timeline_time - scene.start, 0.0, scene.duration);
        double progress = scene.duration > 0.0 ? local / scene.duration : 1.0;
        double transition_progress = scene.transition > 0.0
                                         ? std::clamp(local / scene.transition,
                                                      0.0,
                                                      1.0)
                                         : 1.0;
        return {index,
                scene.name,
                {time_seconds, local, progress},
                transition_progress,
                true};
    }

    void SceneSequence::ensure_buffers(int width, int height) {
        if (previous_frame_.width != width || previous_frame_.height != height)
            previous_frame_ = Frame(width, height);
        if (current_frame_.width != width || current_frame_.height != height)
            current_frame_ = Frame(width, height);
    }

    void SceneSequence::render(Frame &target, double time_seconds) {
        SceneSample current = sample(time_seconds);
        if (!current.valid)
            return;

        const Scene &scene = scenes_[current.index];
        const bool transitioning = current.index > 0 &&
                                   current.transition_progress < 1.0;
        if (!transitioning) {
            scene.render(target, current.time);
            return;
        }

        ensure_buffers(target.width, target.height);
        previous_frame_.pixels = target.pixels;
        current_frame_.pixels = target.pixels;

        const Scene &previous = scenes_[current.index - 1];
        SceneTime previous_time{
            time_seconds,
            previous.duration,
            1.0,
        };
        previous.render(previous_frame_, previous_time);
        scene.render(current_frame_, current.time);
        blend_frames(previous_frame_,
                     current_frame_,
                     target,
                     current.transition_progress);
    }

} // namespace panim
