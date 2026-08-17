#include "panim/EquationMorph.hpp"
#include "panim/Log.hpp"
#include "panim/SvgRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <string_view>
#include <tinyxml2.h>
#include <utility>
#include <vector>

namespace panim {

    namespace {

        struct ParsedLayer {
            std::string svg_markup;
            std::string shape_key;
        };

        struct ParsedLayout {
            double width = 0.0;
            double height = 0.0;
            std::vector<ParsedLayer> layers;
        };

        double smoothstep(double value) {
            double t = std::clamp(value, 0.0, 1.0);
            return t * t * (3.0 - 2.0 * t);
        }

        int interpolate_dimension(int from, int to, double progress) {
            return std::max(1, static_cast<int>(std::lround(
                                   from + (to - from) * progress)));
        }

        bool is_text_expression(const std::string &latex) {
            size_t start = latex.find_first_not_of(" \t\r\n");
            return start != std::string::npos &&
                   latex.compare(start, 6, "\\text{") == 0;
        }

        std::string print_node(const tinyxml2::XMLNode &node) {
            tinyxml2::XMLPrinter printer(nullptr, true);
            node.Accept(&printer);
            return printer.CStr();
        }

        const tinyxml2::XMLElement *find_descendant(
            const tinyxml2::XMLElement *element,
            std::string_view name) {
            if (!element)
                return nullptr;
            if (element->Name() && name == element->Name())
                return element;

            for (const tinyxml2::XMLElement *child = element->FirstChildElement();
                 child;
                 child = child->NextSiblingElement()) {
                if (const auto *match = find_descendant(child, name))
                    return match;
            }
            return nullptr;
        }

        const tinyxml2::XMLElement *find_element_by_id(
            const tinyxml2::XMLElement *element,
            std::string_view id) {
            if (!element)
                return nullptr;
            const char *element_id = element->Attribute("id");
            if (element_id && id == element_id)
                return element;

            for (const tinyxml2::XMLElement *child = element->FirstChildElement();
                 child;
                 child = child->NextSiblingElement()) {
                if (const auto *match = find_element_by_id(child, id))
                    return match;
            }
            return nullptr;
        }

        void append_shape_key(const tinyxml2::XMLElement *element,
                              std::string &key) {
            if (!element)
                return;

            if (element->Name()) {
                key.append(element->Name());
                key.push_back(':');
            }
            if (const char *path = element->Attribute("d"))
                key.append(path);
            if (const char *transform = element->Attribute("transform")) {
                key.append("@transform=");
                key.append(transform);
            }
            key.push_back('|');

            for (const tinyxml2::XMLElement *child = element->FirstChildElement();
                 child;
                 child = child->NextSiblingElement()) {
                append_shape_key(child, key);
            }
        }

        std::string make_layer_svg(const tinyxml2::XMLElement &root,
                                   const tinyxml2::XMLElement *defs,
                                   const tinyxml2::XMLElement &layer) {
            const char *width = root.Attribute("width");
            const char *height = root.Attribute("height");
            const char *view_box = root.Attribute("viewBox");
            if (!width || !height)
                return {};

            std::ostringstream svg;
            svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                   "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                << "width=\"" << width << "\" height=\"" << height << "\" ";
            if (view_box) {
                svg << "viewBox=\"" << view_box << "\"";
            } else {
                svg << "viewBox=\"0 0 " << width << ' ' << height << "\"";
            }
            svg << '>';
            if (defs)
                svg << print_node(*defs);
            svg << print_node(layer) << "</svg>";
            return svg.str();
        }

        bool parse_microtex_layout(const std::filesystem::path &path,
                                   ParsedLayout &layout) {
            tinyxml2::XMLDocument document;
            if (document.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS)
                return false;

            const tinyxml2::XMLElement *root = document.RootElement();
            if (!root || !root->Name() || std::string_view(root->Name()) != "svg")
                return false;

            layout.width = root->DoubleAttribute("width");
            layout.height = root->DoubleAttribute("height");
            if (layout.width <= 0.0 || layout.height <= 0.0)
                return false;

            const tinyxml2::XMLElement *defs = root->FirstChildElement("defs");
            for (const tinyxml2::XMLElement *element = root->FirstChildElement();
                 element;
                 element = element->NextSiblingElement()) {
                if (element == defs)
                    continue;

                ParsedLayer layer;
                layer.svg_markup = make_layer_svg(*root, defs, *element);
                if (layer.svg_markup.empty())
                    return false;

                const tinyxml2::XMLElement *use = find_descendant(element, "use");
                if (use) {
                    const char *href = use->Attribute("xlink:href");
                    if (!href)
                        href = use->Attribute("href");
                    if (href && href[0] == '#' && defs) {
                        const auto *definition = find_element_by_id(defs, href + 1);
                        append_shape_key(definition, layer.shape_key);
                    }
                }

                if (layer.shape_key.empty()) {
                    layer.shape_key = "primitive:";
                    layer.shape_key += print_node(*element);
                }
                layout.layers.push_back(std::move(layer));
            }
            return !layout.layers.empty();
        }

        // Blend two frames of equal size into out (also equal size).
        void blend_frames(const Frame &a, const Frame &b, Frame &out, double t) {
            const double clamped = std::clamp(t, 0.0, 1.0);
            const int size = a.width * a.height;
            for (int i = 0; i < size; ++i) {
                const uint8_t *pa = a.pixels.data() + i * 4;
                const uint8_t *pb = b.pixels.data() + i * 4;
                uint8_t *po = out.pixels.data() + i * 4;

                double alpha_a = pa[3] / 255.0;
                double alpha_b = pb[3] / 255.0;
                double mixed_alpha =
                    (1.0 - clamped) * alpha_a + clamped * alpha_b;

                double r = (1.0 - clamped) * (pa[0] * alpha_a) +
                           clamped * (pb[0] * alpha_b);
                double g = (1.0 - clamped) * (pa[1] * alpha_a) +
                           clamped * (pb[1] * alpha_b);
                double b = (1.0 - clamped) * (pa[2] * alpha_a) +
                           clamped * (pb[2] * alpha_b);

                if (mixed_alpha > 0.0001) {
                    po[0] = static_cast<uint8_t>(r / mixed_alpha);
                    po[1] = static_cast<uint8_t>(g / mixed_alpha);
                    po[2] = static_cast<uint8_t>(b / mixed_alpha);
                    po[3] = static_cast<uint8_t>(mixed_alpha * 255.0);
                } else {
                    po[0] = po[1] = po[2] = 0;
                    po[3] = 0;
                }
            }
        }

        void alpha_over(const Frame &src,
                        Frame &dst,
                        int x0,
                        int y0,
                        double opacity = 1.0,
                        bool tint_set = false,
                        uint8_t tint_r = 255,
                        uint8_t tint_g = 255,
                        uint8_t tint_b = 255) {
            if (opacity <= 0.0)
                return;
            for (int y = 0; y < src.height; ++y) {
                int dy = y0 + y;
                if (dy < 0 || dy >= dst.height)
                    continue;
                for (int x = 0; x < src.width; ++x) {
                    int dx = x0 + x;
                    if (dx < 0 || dx >= dst.width)
                        continue;
                    const uint8_t *sp =
                        src.pixels.data() + (y * src.width + x) * 4;
                    double alpha = (sp[3] / 255.0) * opacity;
                    if (alpha < 0.0001)
                        continue;
                    double source_r = tint_set
                                          ? sp[0] * (tint_r / 255.0)
                                          : sp[0];
                    double source_g = tint_set
                                          ? sp[1] * (tint_g / 255.0)
                                          : sp[1];
                    double source_b = tint_set
                                          ? sp[2] * (tint_b / 255.0)
                                          : sp[2];
                    uint8_t *dp = dst.pixel_ptr(dx, dy);
                    dp[0] = static_cast<uint8_t>(
                        source_r * alpha + dp[0] * (1.0 - alpha));
                    dp[1] = static_cast<uint8_t>(
                        source_g * alpha + dp[1] * (1.0 - alpha));
                    dp[2] = static_cast<uint8_t>(
                        source_b * alpha + dp[2] * (1.0 - alpha));
                    dp[3] = 255;
                }
            }
        }

        bool crop_to_alpha(Frame &frame, int &offset_x, int &offset_y) {
            int min_x = frame.width;
            int min_y = frame.height;
            int max_x = -1;
            int max_y = -1;
            for (int y = 0; y < frame.height; ++y) {
                for (int x = 0; x < frame.width; ++x) {
                    const uint8_t *pixel = frame.pixel_ptr(x, y);
                    if (pixel[3] == 0)
                        continue;
                    min_x = std::min(min_x, x);
                    min_y = std::min(min_y, y);
                    max_x = std::max(max_x, x);
                    max_y = std::max(max_y, y);
                }
            }
            if (max_x < min_x || max_y < min_y)
                return false;

            offset_x = min_x;
            offset_y = min_y;
            Frame cropped(max_x - min_x + 1, max_y - min_y + 1);
            cropped.clear(0, 0, 0, 0);
            for (int y = 0; y < cropped.height; ++y) {
                for (int x = 0; x < cropped.width; ++x) {
                    const uint8_t *source = frame.pixel_ptr(min_x + x, min_y + y);
                    uint8_t *target = cropped.pixel_ptr(x, y);
                    target[0] = source[0];
                    target[1] = source[1];
                    target[2] = source[2];
                    target[3] = source[3];
                }
            }
            frame = std::move(cropped);
            return true;
        }

        void alpha_over_scaled(const Frame &src,
                               Frame &dst,
                               int x0,
                               int y0,
                               int output_width,
                               int output_height,
                               double opacity,
                               bool tint_set,
                               uint8_t tint_r,
                               uint8_t tint_g,
                               uint8_t tint_b) {
            if (opacity <= 0.0 || output_width <= 0 || output_height <= 0 ||
                src.width <= 0 || src.height <= 0) {
                return;
            }
            if (output_width == src.width && output_height == src.height) {
                alpha_over(src,
                           dst,
                           x0,
                           y0,
                           opacity,
                           tint_set,
                           tint_r,
                           tint_g,
                           tint_b);
                return;
            }

            for (int y = 0; y < output_height; ++y) {
                int destination_y = y0 + y;
                if (destination_y < 0 || destination_y >= dst.height)
                    continue;
                double source_y =
                    (static_cast<double>(y) + 0.5) * src.height / output_height - 0.5;
                int y1 = static_cast<int>(std::floor(source_y));
                int y2 = y1 + 1;
                double fy = source_y - y1;
                y1 = std::clamp(y1, 0, src.height - 1);
                y2 = std::clamp(y2, 0, src.height - 1);

                for (int x = 0; x < output_width; ++x) {
                    int destination_x = x0 + x;
                    if (destination_x < 0 || destination_x >= dst.width)
                        continue;
                    double source_x =
                        (static_cast<double>(x) + 0.5) * src.width / output_width - 0.5;
                    int x1 = static_cast<int>(std::floor(source_x));
                    int x2 = x1 + 1;
                    double fx = source_x - x1;
                    x1 = std::clamp(x1, 0, src.width - 1);
                    x2 = std::clamp(x2, 0, src.width - 1);

                    const uint8_t *pixels[4] = {
                        src.pixel_ptr(x1, y1),
                        src.pixel_ptr(x2, y1),
                        src.pixel_ptr(x1, y2),
                        src.pixel_ptr(x2, y2),
                    };
                    const double weights[4] = {
                        (1.0 - fx) * (1.0 - fy),
                        fx * (1.0 - fy),
                        (1.0 - fx) * fy,
                        fx * fy,
                    };

                    double alpha = 0.0;
                    double red = 0.0;
                    double green = 0.0;
                    double blue = 0.0;
                    for (int sample = 0; sample < 4; ++sample) {
                        double sample_alpha = pixels[sample][3] / 255.0;
                        double weight = weights[sample];
                        alpha += sample_alpha * weight;
                        red += pixels[sample][0] * sample_alpha * weight;
                        green += pixels[sample][1] * sample_alpha * weight;
                        blue += pixels[sample][2] * sample_alpha * weight;
                    }
                    if (alpha < 0.0001)
                        continue;

                    red /= alpha;
                    green /= alpha;
                    blue /= alpha;
                    if (tint_set) {
                        red *= tint_r / 255.0;
                        green *= tint_g / 255.0;
                        blue *= tint_b / 255.0;
                    }
                    alpha *= opacity;
                    uint8_t *target = dst.pixel_ptr(destination_x, destination_y);
                    target[0] = static_cast<uint8_t>(
                        red * alpha + target[0] * (1.0 - alpha));
                    target[1] = static_cast<uint8_t>(
                        green * alpha + target[1] * (1.0 - alpha));
                    target[2] = static_cast<uint8_t>(
                        blue * alpha + target[2] * (1.0 - alpha));
                    target[3] = 255;
                }
            }
        }

        template <typename Layer>
        double normalized_layer_x(const Layer &layer, int content_width) {
            return (layer.offset_x + layer.frame.width * 0.5) /
                   std::max(1.0, static_cast<double>(content_width));
        }

        template <typename Layer>
        double normalized_layer_y(const Layer &layer, int content_height) {
            return (layer.offset_y + layer.frame.height * 0.5) /
                   std::max(1.0, static_cast<double>(content_height));
        }

        Frame pad_to(const Frame &src, int width, int height) {
            Frame out(width, height);
            out.clear(0, 0, 0, 0);
            int offset_x = (width - src.width) / 2;
            int offset_y = (height - src.height) / 2;
            for (int y = 0; y < src.height; ++y) {
                for (int x = 0; x < src.width; ++x) {
                    const uint8_t *sp =
                        src.pixels.data() + (y * src.width + x) * 4;
                    uint8_t *dp = out.pixel_ptr(offset_x + x, offset_y + y);
                    dp[0] = sp[0];
                    dp[1] = sp[1];
                    dp[2] = sp[2];
                    dp[3] = sp[3];
                }
            }
            return out;
        }

    } // namespace

    Status EquationMorph::init(const std::string &from_latex,
                               const std::string &to_latex,
                               LatexRenderer &renderer,
                               double scale,
                               int target_height_px) {
        EquationMorphSizing sizing;
        sizing.from_scale = scale;
        sizing.to_scale = scale;
        sizing.from_height_px = target_height_px;
        sizing.to_height_px = target_height_px;
        return init(from_latex, to_latex, renderer, sizing);
    }

    Status EquationMorph::init(const std::string &from_latex,
                               const std::string &to_latex,
                               LatexRenderer &renderer,
                               const EquationMorphSizing &sizing) {
        ready_flag = false;
        matching_ready = false;
        from_layers.clear();
        to_layers.clear();
        error.clear();
        text_transition = is_text_expression(from_latex) &&
                          is_text_expression(to_latex);

        std::filesystem::path svg_from;
        std::filesystem::path svg_to;
        auto from_status = renderer.render_svg(from_latex, svg_from);
        if (!from_status.ok) {
            error = from_status.message;
            return Status::failure(error);
        }
        auto to_status = renderer.render_svg(to_latex, svg_to);
        if (!to_status.ok) {
            error = to_status.message;
            return Status::failure(error);
        }

        if (!std::isfinite(sizing.from_scale) || sizing.from_scale <= 0.0 ||
            !std::isfinite(sizing.to_scale) || sizing.to_scale <= 0.0) {
            error = "EquationMorph endpoint scales must be positive";
            return Status::failure(error);
        }

        double from_scale = sizing.from_scale;
        double to_scale = sizing.to_scale;
        if (sizing.from_height_px > 0 || sizing.to_height_px > 0) {
            int from_width = 0;
            int from_height = 0;
            int to_width = 0;
            int to_height = 0;
            if (!svg_dimensions(svg_from, from_width, from_height) ||
                !svg_dimensions(svg_to, to_width, to_height) ||
                from_height <= 0 || to_height <= 0) {
                error = "Could not measure equation SVGs";
                return Status::failure(error);
            }
            if (sizing.from_height_px > 0) {
                from_scale = static_cast<double>(sizing.from_height_px) /
                             static_cast<double>(from_height);
            }
            if (sizing.to_height_px > 0) {
                to_scale = static_cast<double>(sizing.to_height_px) /
                           static_cast<double>(to_height);
            }
        }

        Frame from_raster(1, 1);
        Frame to_raster(1, 1);
        if (!rasterize_svg(svg_from, from_raster, from_scale)) {
            error = "rasterize from failed";
            return Status::failure(error);
        }
        if (!rasterize_svg(svg_to, to_raster, to_scale)) {
            error = "rasterize to failed";
            return Status::failure(error);
        }

        from_content_width = from_raster.width;
        from_content_height = from_raster.height;
        to_content_width = to_raster.width;
        to_content_height = to_raster.height;
        from_content_frame = from_raster;
        to_content_frame = to_raster;
        int output_width = std::max(from_raster.width, to_raster.width);
        int output_height = std::max(from_raster.height, to_raster.height);
        from_frame = pad_to(from_raster, output_width, output_height);
        to_frame = pad_to(to_raster, output_width, output_height);

        ParsedLayout from_layout;
        ParsedLayout to_layout;
        if (!text_transition &&
            parse_microtex_layout(svg_from, from_layout) &&
            parse_microtex_layout(svg_to, to_layout)) {
            bool layers_ok = true;
            for (const ParsedLayer &parsed : from_layout.layers) {
                Frame frame(1, 1);
                if (!rasterize_svg_data(parsed.svg_markup, frame, from_scale)) {
                    layers_ok = false;
                    break;
                }
                GlyphLayer layer;
                layer.frame = std::move(frame);
                if (!crop_to_alpha(layer.frame, layer.offset_x, layer.offset_y)) {
                    layers_ok = false;
                    break;
                }
                from_layers.push_back(std::move(layer));
            }
            if (layers_ok) {
                for (const ParsedLayer &parsed : to_layout.layers) {
                    Frame frame(1, 1);
                    if (!rasterize_svg_data(parsed.svg_markup, frame, to_scale)) {
                        layers_ok = false;
                        break;
                    }
                    GlyphLayer layer;
                    layer.frame = std::move(frame);
                    if (!crop_to_alpha(layer.frame, layer.offset_x, layer.offset_y)) {
                        layers_ok = false;
                        break;
                    }
                    to_layers.push_back(std::move(layer));
                }
            }

            int match_count = 0;
            int pair_count = 0;
            if (layers_ok) {
                for (size_t from_index = 0;
                     from_index < from_layout.layers.size();
                     ++from_index) {
                    int best_index = -1;
                    double best_distance = std::numeric_limits<double>::max();
                    const ParsedLayer &source = from_layout.layers[from_index];
                    for (size_t to_index = 0;
                         to_index < to_layout.layers.size();
                         ++to_index) {
                        if (to_layers[to_index].matched ||
                            source.shape_key != to_layout.layers[to_index].shape_key) {
                            continue;
                        }
                        double source_x = normalized_layer_x(
                            from_layers[from_index], from_content_width);
                        double source_y = normalized_layer_y(
                            from_layers[from_index], from_content_height);
                        double target_x = normalized_layer_x(
                            to_layers[to_index], to_content_width);
                        double target_y = normalized_layer_y(
                            to_layers[to_index], to_content_height);
                        double dx = target_x - source_x;
                        double dy = target_y - source_y;
                        double distance = dx * dx + dy * dy;
                        if (distance < best_distance) {
                            best_distance = distance;
                            best_index = static_cast<int>(to_index);
                        }
                    }
                    if (best_index >= 0) {
                        from_layers[from_index].match_index = best_index;
                        to_layers[best_index].matched = true;
                        ++match_count;
                    }
                }

                // A text transition often has fewer identical outlines than
                // an equation transition. Pair the remaining layers by
                // normalized proximity so different letters share a motion
                // path and cross-shape instead of leaving a visual hole.
                for (size_t from_index = 0;
                     from_index < from_layout.layers.size();
                     ++from_index) {
                    if (from_layers[from_index].match_index >= 0)
                        continue;

                    int best_index = -1;
                    double best_distance = std::numeric_limits<double>::max();
                    for (size_t to_index = 0;
                         to_index < to_layout.layers.size();
                         ++to_index) {
                        if (to_layers[to_index].matched ||
                            to_layers[to_index].paired) {
                            continue;
                        }
                        double source_x = normalized_layer_x(
                            from_layers[from_index], from_content_width);
                        double source_y = normalized_layer_y(
                            from_layers[from_index], from_content_height);
                        double target_x = normalized_layer_x(
                            to_layers[to_index], to_content_width);
                        double target_y = normalized_layer_y(
                            to_layers[to_index], to_content_height);
                        double dx = target_x - source_x;
                        double dy = target_y - source_y;
                        double distance = dx * dx + dy * dy;
                        if (distance < best_distance) {
                            best_distance = distance;
                            best_index = static_cast<int>(to_index);
                        }
                    }
                    if (best_index >= 0) {
                        from_layers[from_index].pair_index = best_index;
                        to_layers[best_index].paired = true;
                        ++pair_count;
                    }
                }
                matching_ready = true;
                PANIM_LOG_INFO(
                    "EquationMorph: matched {} and paired {} of {} source layers "
                    "({} target layers)",
                    match_count,
                    pair_count,
                    from_layers.size(),
                    to_layers.size());
            } else {
                from_layers.clear();
                to_layers.clear();
            }
        }

        if (text_transition) {
            PANIM_LOG_INFO(
                "EquationMorph: using size-aware text transition");
        } else if (!matching_ready) {
            PANIM_LOG_WARN(
                "EquationMorph: glyph parsing unavailable; using whole-equation crossfade");
        }

        ready_flag = true;
        return Status::success();
    }

    void EquationMorph::render(Frame &target, double t) const {
        if (!ready_flag)
            return;

        double clamped = std::clamp(t, 0.0, 1.0);
        int center_position_x = center_x;
        int center_position_y = center_y;
        if (use_norm_center) {
            center_position_x = static_cast<int>(center_norm_x * target.width);
            center_position_y = static_cast<int>(center_norm_y * target.height);
        }
        int base_x = center_position_x - from_frame.width / 2;
        int base_y = center_position_y - from_frame.height / 2;

        if (clamped <= 0.0) {
            alpha_over(from_frame,
                       target,
                       base_x,
                       base_y,
                       1.0,
                       tint_set,
                       tint_r,
                       tint_g,
                       tint_b);
            return;
        }
        if (clamped >= 1.0) {
            alpha_over(to_frame,
                       target,
                       base_x,
                       base_y,
                       1.0,
                       tint_set,
                       tint_r,
                       tint_g,
                       tint_b);
            return;
        }

        double eased = smoothstep(clamped);
        if (!matching_ready && !text_transition) {
            Frame blended(from_frame.width, from_frame.height);
            blended.clear(0, 0, 0, 0);
            blend_frames(from_frame, to_frame, blended, eased);
            alpha_over(blended,
                       target,
                       base_x,
                       base_y,
                       1.0,
                       tint_set,
                       tint_r,
                       tint_g,
                       tint_b);
            return;
        }

        if (text_transition) {
            int content_width = interpolate_dimension(
                from_content_width, to_content_width, eased);
            int content_height = interpolate_dimension(
                from_content_height, to_content_height, eased);
            int separation = static_cast<int>(std::lround(
                std::sin(clamped * 3.14159265358979323846) *
                std::max(from_content_height, to_content_height) * 0.72));
            double outgoing_opacity =
                1.0 - smoothstep((clamped - 0.35) / 0.65);
            double incoming_opacity = smoothstep(clamped / 0.65);
            int content_x = center_position_x - content_width / 2;
            int content_y = center_position_y - content_height / 2;
            alpha_over_scaled(from_content_frame,
                              target,
                              content_x,
                              content_y - separation,
                              content_width,
                              content_height,
                              outgoing_opacity,
                              tint_set,
                              tint_r,
                              tint_g,
                              tint_b);
            alpha_over_scaled(to_content_frame,
                              target,
                              content_x,
                              content_y + separation,
                              content_width,
                              content_height,
                              incoming_opacity,
                              tint_set,
                              tint_r,
                              tint_g,
                              tint_b);
            return;
        }

        int drift = std::max(
            3, static_cast<int>(std::lround(from_frame.height * 0.08)));
        int from_padding_x = (from_frame.width - from_content_width) / 2;
        int from_padding_y = (from_frame.height - from_content_height) / 2;
        int to_padding_x = (to_frame.width - to_content_width) / 2;
        int to_padding_y = (to_frame.height - to_content_height) / 2;

        for (const GlyphLayer &source : from_layers) {
            double source_x = base_x + from_padding_x + source.offset_x;
            double source_y = base_y + from_padding_y + source.offset_y;
            if (source.match_index >= 0) {
                const GlyphLayer &destination = to_layers[source.match_index];
                double destination_x =
                    base_x + to_padding_x + destination.offset_x;
                double destination_y =
                    base_y + to_padding_y + destination.offset_y;
                int layer_x = static_cast<int>(std::lround(
                    source_x + (destination_x - source_x) * eased));
                int layer_y = static_cast<int>(std::lround(
                    source_y + (destination_y - source_y) * eased));
                int layer_width = interpolate_dimension(
                    source.frame.width, destination.frame.width, eased);
                int layer_height = interpolate_dimension(
                    source.frame.height, destination.frame.height, eased);
                const Frame &shape =
                    source.frame.width * source.frame.height >=
                            destination.frame.width * destination.frame.height
                        ? source.frame
                        : destination.frame;
                alpha_over_scaled(shape,
                                  target,
                                  layer_x,
                                  layer_y,
                                  layer_width,
                                  layer_height,
                                  1.0,
                                  tint_set,
                                  tint_r,
                                  tint_g,
                                  tint_b);
            } else if (source.pair_index >= 0) {
                const GlyphLayer &destination = to_layers[source.pair_index];
                double destination_x =
                    base_x + to_padding_x + destination.offset_x;
                double destination_y =
                    base_y + to_padding_y + destination.offset_y;
                int layer_x = static_cast<int>(std::lround(
                    source_x + (destination_x - source_x) * eased));
                int layer_y = static_cast<int>(std::lround(
                    source_y + (destination_y - source_y) * eased));
                int layer_width = interpolate_dimension(
                    source.frame.width, destination.frame.width, eased);
                int layer_height = interpolate_dimension(
                    source.frame.height, destination.frame.height, eased);
                alpha_over_scaled(source.frame,
                                  target,
                                  layer_x,
                                  layer_y,
                                  layer_width,
                                  layer_height,
                                  1.0 - eased,
                                  tint_set,
                                  tint_r,
                                  tint_g,
                                  tint_b);
                alpha_over_scaled(destination.frame,
                                  target,
                                  layer_x,
                                  layer_y,
                                  layer_width,
                                  layer_height,
                                  eased,
                                  tint_set,
                                  tint_r,
                                  tint_g,
                                  tint_b);
            } else {
                int layer_x = static_cast<int>(std::lround(source_x));
                int layer_y = static_cast<int>(std::lround(source_y));
                layer_y -= static_cast<int>(std::lround(drift * eased));
                alpha_over(source.frame,
                           target,
                           layer_x,
                           layer_y,
                           1.0 - eased,
                           tint_set,
                           tint_r,
                           tint_g,
                           tint_b);
            }
        }

        for (const GlyphLayer &destination : to_layers) {
            if (destination.matched || destination.paired)
                continue;
            int layer_x = base_x + to_padding_x + destination.offset_x;
            int layer_y = base_y + to_padding_y + destination.offset_y +
                          static_cast<int>(std::lround(drift * (1.0 - eased)));
            alpha_over(destination.frame,
                       target,
                       layer_x,
                       layer_y,
                       eased,
                       tint_set,
                       tint_r,
                       tint_g,
                       tint_b);
        }
    }

} // namespace panim
