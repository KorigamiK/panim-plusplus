#include "panim/LatexTrack.hpp"
#include "panim/Log.hpp"
#include "panim/SvgRenderer.hpp"

#include <algorithm>
#include <cmath>

namespace panim {

    void LatexTrack::add_keyframe(const std::string &latex,
                                  double hold_sec,
                                  double transition_sec,
                                  double height_scale) {
        keys_.push_back({latex, hold_sec, transition_sec, height_scale});
    }

    Status LatexTrack::prepare(LatexRenderer &renderer, int frame_height) {
        ready_ = false;
        frames_.clear();
        morphs_.clear();
        segments_.clear();
        total_duration_ = 0.0;
        if (keys_.empty())
            return Status::failure("No keyframes");

        int target_h = static_cast<int>(std::lround(
            frame_height * target_height_ratio_));
        std::vector<double> raster_scales;
        raster_scales.reserve(keys_.size());
        for (const auto &k : keys_) {
            if (!std::isfinite(k.height_scale) || k.height_scale <= 0.0) {
                return Status::failure(
                    "LatexTrack keyframe height scale must be positive");
            }
            int key_target_h = target_h > 0
                                   ? std::max(1, static_cast<int>(std::lround(
                                                     target_h * k.height_scale)))
                                   : -1;

            std::filesystem::path svg_path;
            auto st = renderer.render_svg(k.latex, svg_path);
            if (!st.ok)
                return st;

            int w0 = 0, h0 = 0;
            double scale = k.height_scale;
            if (key_target_h > 0 && svg_dimensions(svg_path, w0, h0) && h0 > 0) {
                scale = static_cast<double>(key_target_h) /
                        static_cast<double>(h0);
            }
            Frame f(1, 1);
            if (!rasterize_svg(svg_path, f, scale)) {
                return Status::failure("rasterize failed");
            }
            long alpha_sum = 0;
            for (size_t i = 0; i + 3 < f.pixels.size(); i += 4)
                alpha_sum += f.pixels[i + 3];
            if (alpha_sum == 0) {
                PANIM_LOG_ERROR(
                    "LatexTrack: empty alpha after rasterize for {} ({}x{})",
                    k.latex,
                    f.width,
                    f.height);
            }
            raster_scales.push_back(scale);
            frames_.push_back(std::move(f));
        }

        for (size_t i = 1; i < keys_.size(); ++i) {
            EquationMorph morph;
            EquationMorphSizing sizing;
            sizing.from_scale = raster_scales[i - 1];
            sizing.to_scale = raster_scales[i];
            auto status = morph.init(keys_[i - 1].latex,
                                     keys_[i].latex,
                                     renderer,
                                     sizing);
            if (!status.ok) {
                PANIM_LOG_WARN("LatexTrack: glyph morph unavailable for transition {}: {}",
                               i - 1,
                               status.message);
            }
            morph.set_center_norm(center_norm_x_, center_norm_y_);
            if (tint_set_)
                morph.set_tint(tint_r_, tint_g_, tint_b_);
            morphs_.push_back(std::move(morph));
        }

        // Build segments: initial hold of key0
        double t = 0.0;
        segments_.push_back({0, 0, t, t + keys_[0].hold_sec, false});
        t += keys_[0].hold_sec;
        for (size_t i = 1; i < keys_.size(); ++i) {
            segments_.push_back({static_cast<int>(i - 1),
                                 static_cast<int>(i),
                                 t,
                                 t + keys_[i].transition_sec,
                                 true});
            t += keys_[i].transition_sec;
            segments_.push_back({static_cast<int>(i),
                                 static_cast<int>(i),
                                 t,
                                 t + keys_[i].hold_sec,
                                 false});
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
        auto smoothstep = [](double value) {
            double eased = std::clamp(value, 0.0, 1.0);
            return eased * eased * (3.0 - 2.0 * eased);
        };
        double weight_a = 1.0 - smoothstep(u * 2.0);
        double weight_b = smoothstep((u - 0.5) * 2.0);
        int output_width = std::max(a.width, b.width);
        int output_height = std::max(a.height, b.height);
        int offset_a_x = (output_width - a.width) / 2;
        int offset_a_y = (output_height - a.height) / 2;
        int offset_b_x = (output_width - b.width) / 2;
        int offset_b_y = (output_height - b.height) / 2;

        Frame out(output_width, output_height);
        for (int y = 0; y < output_height; ++y) {
            for (int x = 0; x < output_width; ++x) {
                int ax = x - offset_a_x;
                int ay = y - offset_a_y;
                int bx = x - offset_b_x;
                int by = y - offset_b_y;

                const uint8_t *pa = nullptr;
                const uint8_t *pb = nullptr;
                if (ax >= 0 && ax < a.width && ay >= 0 && ay < a.height) {
                    pa = a.pixels.data() +
                         static_cast<size_t>((ay * a.width + ax) * 4);
                }
                if (bx >= 0 && bx < b.width && by >= 0 && by < b.height) {
                    pb = b.pixels.data() +
                         static_cast<size_t>((by * b.width + bx) * 4);
                }

                double aa = pa ? pa[3] / 255.0 : 0.0;
                double ab = pb ? pb[3] / 255.0 : 0.0;
                double am = weight_a * aa + weight_b * ab;
                double r = weight_a * (pa ? pa[0] : 0) * aa +
                           weight_b * (pb ? pb[0] : 0) * ab;
                double g = weight_a * (pa ? pa[1] : 0) * aa +
                           weight_b * (pb ? pb[1] : 0) * ab;
                double bl = weight_a * (pa ? pa[2] : 0) * aa +
                            weight_b * (pb ? pb[2] : 0) * ab;
                size_t idx = static_cast<size_t>((y * output_width + x) * 4);
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
            alpha_over_center(frames_[seg->from_idx],
                              target,
                              center_norm_x_,
                              center_norm_y_,
                              tint_set_,
                              tint_r_,
                              tint_g_,
                              tint_b_);
        } else {
            double local_t = (tt - seg->start) / (seg->end - seg->start);
            size_t morph_index = static_cast<size_t>(seg->to_idx - 1);
            if (morph_index < morphs_.size() && morphs_[morph_index].ready()) {
                morphs_[morph_index].render(target, local_t);
                return;
            }
            Frame blended = blend_frames(
                frames_[seg->from_idx], frames_[seg->to_idx], local_t);
            alpha_over_center(blended,
                              target,
                              center_norm_x_,
                              center_norm_y_,
                              tint_set_,
                              tint_r_,
                              tint_g_,
                              tint_b_);
        }
    }

} // namespace panim
