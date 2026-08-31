#include <algorithm>
#include <cmath>

#include "panim/Animation.hpp"
#include "panim/Frame.hpp"
#include "panim/LatexTrack.hpp"
#include "panim/Log.hpp"
#include "panim/Painter.hpp"
#include "panim/Plugin.hpp"

namespace {

    class MatrixCal : public panim::Animation {
    public:
        panim::AnimationInfo info() const override {
            return {"Matrix cal", 10.0, 1920, 1080, 30.0};
        }

        void on_setup(const panim::AnimationContext &ctx) override {
            ctx_ = ctx;
            if (!ctx.latex) {
                PANIM_LOG_ERROR("MatrixCal: LatexRenderer not available");
                return;
            }

            title_.set_center_norm(0.5, 0.13);
            title_.set_target_height_ratio(0.065);
            title_.set_tint(116, 232, 205);
            title_.add_keyframe("\\text{Matrix multiplication}", 10.0, 0.0);

            calculation_.set_center_norm(0.5, 0.52);
            calculation_.set_target_height_ratio(0.22);
            calculation_.add_keyframe(
                "A=\\begin{bmatrix}1&2\\\\3&4\\end{bmatrix}\\quad "
                "B=\\begin{bmatrix}2&0\\\\1&2\\end{bmatrix}",
                1.2, 0.0);
            calculation_.add_keyframe(
                "AB=\\begin{bmatrix}1&2\\\\3&4\\end{bmatrix}"
                "\\begin{bmatrix}2&0\\\\1&2\\end{bmatrix}",
                1.0, 0.6);
            calculation_.add_keyframe(
                "c_{11}=1\\cdot2+2\\cdot1=4",
                0.8, 0.5, 0.72);
            calculation_.add_keyframe(
                "c_{12}=1\\cdot0+2\\cdot2=4",
                0.8, 0.5, 0.72);
            calculation_.add_keyframe(
                "c_{21}=3\\cdot2+4\\cdot1=10",
                0.8, 0.5, 0.72);
            calculation_.add_keyframe(
                "c_{22}=3\\cdot0+4\\cdot2=8",
                0.8, 0.5, 0.72);
            calculation_.add_keyframe(
                "AB=\\begin{bmatrix}4&4\\\\10&8\\end{bmatrix}",
                1.7, 0.7, 1.15);

            caption_.set_center_norm(0.5, 0.83);
            caption_.set_target_height_ratio(0.042);
            caption_.set_tint(168, 187, 214);
            caption_.add_keyframe("\\text{row by column}", 10.0, 0.0);

            prepare_track(title_, "title");
            prepare_track(calculation_, "calculation");
            prepare_track(caption_, "caption");
        }

        void render_frame(panim::Frame &frame, double time_seconds) override {
            panim::Painter painter(frame);
            painter.fill_vertical_gradient({5, 12, 25, 255}, {11, 31, 48, 255});

            int spacing = std::max(48, ctx_.width / 24);
            int drift = static_cast<int>(std::fmod(time_seconds * 12.0, spacing));
            for (int x = -ctx_.height; x < ctx_.width + spacing; x += spacing) {
                painter.stroke_line(x + drift, 0, x + drift + ctx_.height, ctx_.height,
                                    1, {68, 184, 166, 255}, 0.07f);
            }

            int glow_radius = static_cast<int>(ctx_.height *
                (0.26 + 0.015 * std::sin(time_seconds * 1.4)));
            painter.fill_circle(ctx_.width / 2, ctx_.height / 2, glow_radius,
                                {39, 176, 150, 255}, 0.08f);

            if (title_.ready())
                title_.render(frame, time_seconds);
            if (calculation_.ready())
                calculation_.render(frame, std::min(time_seconds, calculation_.duration()));
            if (caption_.ready())
                caption_.render(frame, time_seconds);

            double progress = std::clamp(time_seconds / ctx_.duration, 0.0, 1.0);
            int margin = ctx_.width / 12;
            int bar_y = ctx_.height - ctx_.height / 15;
            int bar_width = ctx_.width - margin * 2;
            painter.fill_rect(margin, bar_y, bar_width, 4,
                              {255, 255, 255, 255}, 0.12f);
            painter.fill_rect(margin, bar_y, static_cast<int>(bar_width * progress), 4,
                              {116, 232, 205, 255}, 0.9f);
        }

    private:
        panim::AnimationContext ctx_{};
        panim::LatexTrack title_;
        panim::LatexTrack calculation_;
        panim::LatexTrack caption_;

        void prepare_track(panim::LatexTrack &track, const char *name) {
            panim::Status status = track.prepare(*ctx_.latex, ctx_.height);
            if (!status.ok)
                PANIM_LOG_ERROR("MatrixCal {} prepare failed: {}", name, status.message);
        }
    };

} // namespace

PANIM_EXPORT_ANIMATION(MatrixCal)
