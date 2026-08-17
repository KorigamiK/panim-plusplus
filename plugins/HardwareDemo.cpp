#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

#include "panim/Animation.hpp"
#include "panim/Compute.hpp"
#include "panim/Frame.hpp"
#include "panim/LatexTrack.hpp"
#include "panim/Log.hpp"
#include "panim/Painter.hpp"
#include "panim/Plugin.hpp"

namespace {

    std::string latex_text(std::string text) {
        for (char &character : text) {
            switch (character) {
            case '\\':
            case '{':
            case '}':
            case '$':
            case '&':
            case '#':
            case '%':
            case '_':
            case '^':
                character = ' ';
                break;
            default:
                break;
            }
        }
        return "\\text{" + text + "}";
    }

    panim::Color backend_color(panim::ComputeBackend backend) {
        switch (backend) {
        case panim::ComputeBackend::WebGpu:
            return {100, 220, 255, 255};
        case panim::ComputeBackend::Cpu:
            return {255, 202, 96, 255};
        }
        return {255, 255, 255, 255};
    }

    class HardwareDemo : public panim::Animation {
    public:
        panim::AnimationInfo info() const override {
            return {"HardwareDemo", 4.0, 1280, 720, 30.0};
        }

        void on_setup(const panim::AnimationContext &ctx) override {
            ctx_ = ctx;
            device_ = panim::compute_device();
            accent_ = backend_color(device_.backend);
            PANIM_LOG_INFO("HardwareDemo selected {} on {}",
                           device_.backend_name,
                           device_.device_name);

            if (ctx.latex) {
                title_.set_center_norm(0.5, 0.10);
                title_.set_target_height_ratio(0.065);
                title_.add_keyframe("\\text{panim heterogeneous compute}",
                                    60.0,
                                    0.0);

                backend_.set_center_norm(0.5, 0.19);
                backend_.set_target_height_ratio(0.038);
                backend_.add_keyframe(
                    latex_text("Auto-selected: " + device_.device_name +
                               " via " + device_.backend_name),
                    60.0,
                    0.0);

                footer_.set_center_norm(0.5, 0.91);
                footer_.set_target_height_ratio(0.033);
                footer_.add_keyframe(
                    "\\text{One WGSL kernel / WebGPU / CPU}",
                    60.0,
                    0.0);

                title_ready_ = title_.prepare(*ctx.latex, ctx.height).ok;
                backend_ready_ = backend_.prepare(*ctx.latex, ctx.height).ok;
                footer_ready_ = footer_.prepare(*ctx.latex, ctx.height).ok;
            }
        }

        void render_frame(panim::Frame &frame, double time_sec) override {
            panim::ComputeParams params;
            params.time_seconds = static_cast<float>(time_sec);
            params.strength = 1.0f;
            panim::ComputeResult result = panim::apply_compute_effect(
                frame, panim::ComputeEffect::AnimatedGradient, params);
            if (!result_logged_) {
                if (result.ok) {
                    PANIM_LOG_INFO("HardwareDemo frames executing on {}",
                                   panim::compute_backend_name(result.backend));
                } else {
                    PANIM_LOG_ERROR("HardwareDemo compute failed: {}",
                                    result.message);
                }
                result_logged_ = true;
            }

            panim::Painter painter(frame);
            painter.fill_rect(0,
                              0,
                              ctx_.width,
                              static_cast<int>(ctx_.height * 0.25),
                              {4, 8, 24, 255},
                              0.58f);
            painter.fill_rect(0,
                              static_cast<int>(ctx_.height * 0.84),
                              ctx_.width,
                              static_cast<int>(ctx_.height * 0.16),
                              {4, 8, 24, 255},
                              0.48f);

            draw_compute_graph(frame, time_sec);
            draw_activity_meter(frame, time_sec);

            if (title_ready_)
                title_.render(frame, time_sec);
            if (backend_ready_)
                backend_.render(frame, time_sec);
            if (footer_ready_)
                footer_.render(frame, time_sec);
        }

    private:
        panim::AnimationContext ctx_{};
        panim::ComputeDeviceInfo device_{};
        panim::Color accent_{255, 255, 255, 255};
        panim::LatexTrack title_;
        panim::LatexTrack backend_;
        panim::LatexTrack footer_;
        bool title_ready_ = false;
        bool backend_ready_ = false;
        bool footer_ready_ = false;
        bool result_logged_ = false;

        void draw_compute_graph(panim::Frame &frame, double time_sec) {
            panim::Painter painter(frame);
            int center_x = ctx_.width / 2;
            int center_y = static_cast<int>(ctx_.height * 0.55);
            int horizontal = static_cast<int>(ctx_.width * 0.24);
            int vertical = static_cast<int>(ctx_.height * 0.17);
            int node_radius = std::max(16, static_cast<int>(ctx_.height * 0.055));

            struct Node {
                int x;
                int y;
            };
            Node nodes[] = {
                {center_x - horizontal, center_y - vertical},
                {center_x + horizontal, center_y - vertical},
                {center_x - horizontal, center_y + vertical},
                {center_x + horizontal, center_y + vertical},
            };

            for (const Node &node : nodes) {
                painter.stroke_line(center_x,
                                    center_y,
                                    node.x,
                                    node.y,
                                    4,
                                    accent_,
                                    0.25f);
                painter.stroke_line(center_x,
                                    center_y,
                                    node.x,
                                    node.y,
                                    1,
                                    {255, 255, 255, 255},
                                    0.45f);
            }

            int pulse_radius = node_radius + static_cast<int>(
                                                 node_radius * 0.18 *
                                                 std::sin(time_sec * 2.4));
            painter.fill_circle(center_x,
                                center_y,
                                pulse_radius + 18,
                                accent_,
                                0.16f);
            painter.fill_circle(center_x,
                                center_y,
                                pulse_radius,
                                {8, 14, 32, 255},
                                0.88f);
            painter.fill_circle(center_x,
                                center_y,
                                std::max(5, pulse_radius / 3),
                                accent_,
                                0.92f);

            for (size_t i = 0; i < std::size(nodes); ++i) {
                const Node &node = nodes[i];
                painter.fill_circle(node.x,
                                    node.y,
                                    node_radius + 9,
                                    {8, 14, 32, 255},
                                    0.48f);
                painter.fill_circle(node.x,
                                    node.y,
                                    node_radius,
                                    accent_,
                                    0.76f);

                double progress = std::fmod(time_sec * 0.62 + i * 0.23, 1.0);
                int packet_x = static_cast<int>(
                    node.x + (center_x - node.x) * progress);
                int packet_y = static_cast<int>(
                    node.y + (center_y - node.y) * progress);
                painter.fill_circle(packet_x,
                                    packet_y,
                                    std::max(4, node_radius / 7),
                                    {255, 255, 255, 255},
                                    0.95f);
            }
        }

        void draw_activity_meter(panim::Frame &frame, double time_sec) {
            panim::Painter painter(frame);
            int bar_count = 20;
            int meter_width = static_cast<int>(ctx_.width * 0.56);
            int gap = std::max(3, meter_width / (bar_count * 5));
            int bar_width = (meter_width - gap * (bar_count - 1)) / bar_count;
            int start_x = (ctx_.width - meter_width) / 2;
            int baseline = static_cast<int>(ctx_.height * 0.79);
            int max_height = static_cast<int>(ctx_.height * 0.08);
            for (int i = 0; i < bar_count; ++i) {
                double phase = time_sec * 2.1 + i * 0.47;
                double activity = 0.2 + 0.8 * (0.5 + 0.5 * std::sin(phase));
                int height = static_cast<int>(max_height * activity);
                painter.fill_rect(start_x + i * (bar_width + gap),
                                  baseline - height,
                                  bar_width,
                                  height,
                                  accent_,
                                  0.62f);
            }
        }
    };

} // namespace

PANIM_EXPORT_ANIMATION(HardwareDemo)
