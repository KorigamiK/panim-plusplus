#include "panim/LatexTrack.hpp"
#include "panim/Log.hpp"
#include "panim/SvgRenderer.hpp"

#include <algorithm>

namespace panim {

    void LatexTrack::add_keyframe(const std::string &latex, double hold_sec, double transition_sec) {
        keys_.push_back({latex, hold_sec, transition_sec});
    }

    Status LatexTrack::prepare(LatexRenderer &renderer, int frame_height) {
        ready_ = false;
        frames_.clear();
        segments_.clear();
        total_duration_ = 0.0;
        if (keys_.empty())
            return Status::failure("No keyframes");

        int target_h = static_cast<int>(frame_height * target_height_ratio_);
        for (const auto &k : keys_) {
            std::filesystem::path svg_path;
            auto st = renderer.render_svg(k.latex, svg_path);
            if (!st.ok)
                return st;

            int w0 = 0, h0 = 0;
            double scale = 1.0;
            if (target_h > 0 && svg_dimensions(svg_path, w0, h0) && h0 > 0) {
                scale = static_cast<double>(target_h) / static_cast<double>(h0);
            }
            Frame f(1, 1);
            if (!rasterize_svg(svg_path, f, scale)) {
                return Status::failure("rasterize failed");
            }
            long alpha_sum = 0;
            for (size_t i = 0; i + 3 < f.pixels.size(); i += 4)
                alpha_sum += f.pixels[i + 3];
            if (alpha_sum == 0) {
                PANIM_LOG_ERROR("LatexTrack: empty alpha after rasterize for {} ({}x{})", k.latex, f.width, f.height);
            }
            frames_.push_back(std::move(f));
        }

        // Build segments: initial hold of key0
        double t = 0.0;
        segments_.push_back({0, 0, t, t + keys_[0].hold_sec, false});
        t += keys_[0].hold_sec;
        for (size_t i = 1; i < keys_.size(); ++i) {
            segments_.push_back({static_cast<int>(i - 1), static_cast<int>(i), t, t + keys_[i].transition_sec, true});
            t += keys_[i].transition_sec;
            segments_.push_back({static_cast<int>(i), static_cast<int>(i), t, t + keys_[i].hold_sec, false});
            t += keys_[i].hold_sec;
        }
        total_duration_ = t;
        ready_ = true;
        return Status::success();
    }

    static void alpha_over_center(const Frame &src, Frame &dst, double nx, double ny,
                                  bool tint_set, uint8_t tr, uint8_t tg, uint8_t tb) {
        int x0 = static_cast<int>(nx * dst.width) - src.width / 2;
        int y0 = static_cast<int>(ny * dst.height) - src.height / 2;
        for (int y = 0; y < src.height; ++y) {
            int dy = y0 + y;
            if (dy < 0 || dy >= dst.height)
                continue;
            for (int x = 0; x < src.width; ++x) {
                int dx = x0 + x;
                if (dx < 0 || dx >= dst.width)
                    continue;
                const uint8_t *sp = src.pixels.data() + (y * src.width + x) * 4;
                uint8_t *dp = dst.pixel_ptr(dx, dy);
                float a = sp[3] / 255.0f;
                if (a < 1e-4f)
                    continue;
                uint8_t sr = sp[0], sg = sp[1], sb = sp[2];
                if (tint_set) {
                    sr = static_cast<uint8_t>(sr * (tr / 255.0f));
                    sg = static_cast<uint8_t>(sg * (tg / 255.0f));
                    sb = static_cast<uint8_t>(sb * (tb / 255.0f));
                }
                dp[0] = static_cast<uint8_t>(sr * a + dp[0] * (1.0f - a));
                dp[1] = static_cast<uint8_t>(sg * a + dp[1] * (1.0f - a));
                dp[2] = static_cast<uint8_t>(sb * a + dp[2] * (1.0f - a));
                dp[3] = 255;
            }
        }
    }

    static Frame blend_frames(const Frame &a, const Frame &b, double t) {
        double u = std::clamp(t, 0.0, 1.0);
        Frame out(a.width, a.height);
        for (int y = 0; y < a.height; ++y) {
            for (int x = 0; x < a.width; ++x) {
                size_t idx = static_cast<size_t>((y * a.width + x) * 4);
                const uint8_t *pa = a.pixels.data() + idx;
                const uint8_t *pb = b.pixels.data() + idx;
                double aa = pa[3] / 255.0;
                double ab = pb[3] / 255.0;
                double am = (1.0 - u) * aa + u * ab;
                double r = (1.0 - u) * pa[0] * aa + u * pb[0] * ab;
                double g = (1.0 - u) * pa[1] * aa + u * pb[1] * ab;
                double bl = (1.0 - u) * pa[2] * aa + u * pb[2] * ab;
                uint8_t *po = out.pixels.data() + idx;
                if (am > 1e-4) {
                    po[0] = static_cast<uint8_t>(r / am);
                    po[1] = static_cast<uint8_t>(g / am);
                    po[2] = static_cast<uint8_t>(bl / am);
                    po[3] = static_cast<uint8_t>(am * 255.0);
                } else {
                    po[0] = po[1] = po[2] = 0;
                    po[3] = 0;
                }
            }
        }
        return out;
    }

    void LatexTrack::render(Frame &target, double t) const {
        if (!ready_ || keys_.empty())
            return;
        if (segments_.empty())
            return;

        double tt = std::clamp(t, 0.0, total_duration_ - 1e-6);
        const Segment *seg = nullptr;
        for (const auto &s : segments_) {
            if (tt >= s.start && tt < s.end) {
                seg = &s;
                break;
            }
        }
        if (!seg)
            seg = &segments_.back();

        if (!seg->is_transition) {
            alpha_over_center(frames_[seg->from_idx], target, center_norm_x_, center_norm_y_, tint_set_, tint_r_, tint_g_, tint_b_);
        } else {
            double local_t = (tt - seg->start) / (seg->end - seg->start);
            Frame blended = blend_frames(frames_[seg->from_idx], frames_[seg->to_idx], local_t);
            alpha_over_center(blended, target, center_norm_x_, center_norm_y_, tint_set_, tint_r_, tint_g_, tint_b_);
        }
    }

} // namespace panim
