// Small scene compositor with timeline-local time and automatic crossfades.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "panim/Frame.hpp"

namespace panim {

    struct SceneTime {
        double global_seconds = 0.0;
        double local_seconds = 0.0;
        double progress = 0.0;
    };

    struct SceneSample {
        size_t index = 0;
        std::string_view name;
        SceneTime time;
        double transition_progress = 1.0;
        bool valid = false;
    };

    class SceneSequence {
    public:
        using RenderFn = std::function<void(Frame &, const SceneTime &)>;

        SceneSequence &add(std::string name,
                           double duration,
                           RenderFn render,
                           double transition = 0.65);

        void set_looping(bool looping) { looping_ = looping; }
        bool looping() const { return looping_; }
        bool empty() const { return scenes_.empty(); }
        size_t size() const { return scenes_.size(); }
        double duration() const { return total_duration_; }

        SceneSample sample(double time_seconds) const;
        void render(Frame &target, double time_seconds);

    private:
        struct Scene {
            std::string name;
            double start = 0.0;
            double duration = 0.0;
            double transition = 0.0;
            RenderFn render;
        };

        std::vector<Scene> scenes_;
        Frame previous_frame_{0, 0};
        Frame current_frame_{0, 0};
        double total_duration_ = 0.0;
        bool looping_ = false;

        void ensure_buffers(int width, int height);
    };

} // namespace panim
