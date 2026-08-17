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
            double anchor_x = 0.0;
            double anchor_y = 0.0;
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
                    layer.anchor_x = use->DoubleAttribute("x");
                    layer.anchor_y = use->DoubleAttribute("y");
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
        ready_flag = false;
        matching_ready = false;
        from_layers.clear();
        to_layers.clear();
        error.clear();

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

        // Use one scale for both equations so their relative glyph sizes stay
        // stable, and ensure the taller equation fits the requested height.
        if (target_height_px > 0) {
            int from_width = 0;
            int from_height = 0;
            int to_width = 0;
            int to_height = 0;
            if (svg_dimensions(svg_from, from_width, from_height) &&
                svg_dimensions(svg_to, to_width, to_height)) {
                int max_height = std::max(from_height, to_height);
                if (max_height > 0) {
                    scale = static_cast<double>(target_height_px) /
                            static_cast<double>(max_height);
                }
            }
        }
        raster_scale = scale;

        Frame from_raster(1, 1);
        Frame to_raster(1, 1);
        if (!rasterize_svg(svg_from, from_raster, scale)) {
            error = "rasterize from failed";
            return Status::failure(error);
        }
        if (!rasterize_svg(svg_to, to_raster, scale)) {
            error = "rasterize to failed";
            return Status::failure(error);
        }

        from_content_width = from_raster.width;
        from_content_height = from_raster.height;
        to_content_width = to_raster.width;
        to_content_height = to_raster.height;
        int output_width = std::max(from_raster.width, to_raster.width);
        int output_height = std::max(from_raster.height, to_raster.height);
        from_frame = pad_to(from_raster, output_width, output_height);
        to_frame = pad_to(to_raster, output_width, output_height);

        ParsedLayout from_layout;
        ParsedLayout to_layout;
        if (parse_microtex_layout(svg_from, from_layout) &&
            parse_microtex_layout(svg_to, to_layout)) {
            bool layers_ok = true;
            for (const ParsedLayer &parsed : from_layout.layers) {
                Frame frame(1, 1);
                if (!rasterize_svg_data(parsed.svg_markup, frame, scale)) {
                    layers_ok = false;
                    break;
                }
                GlyphLayer layer;
                layer.frame = std::move(frame);
                layer.anchor_x = parsed.anchor_x;
                layer.anchor_y = parsed.anchor_y;
                from_layers.push_back(std::move(layer));
            }
            if (layers_ok) {
                for (const ParsedLayer &parsed : to_layout.layers) {
                    Frame frame(1, 1);
                    if (!rasterize_svg_data(parsed.svg_markup, frame, scale)) {
                        layers_ok = false;
                        break;
                    }
                    GlyphLayer layer;
                    layer.frame = std::move(frame);
                    layer.anchor_x = parsed.anchor_x;
                    layer.anchor_y = parsed.anchor_y;
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
                        double source_x = source.anchor_x / from_layout.width;
                        double source_y = source.anchor_y / from_layout.height;
                        double target_x =
                            to_layout.layers[to_index].anchor_x / to_layout.width;
                        double target_y =
                            to_layout.layers[to_index].anchor_y / to_layout.height;
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
                    const ParsedLayer &source = from_layout.layers[from_index];
                    for (size_t to_index = 0;
                         to_index < to_layout.layers.size();
                         ++to_index) {
                        if (to_layers[to_index].matched ||
                            to_layers[to_index].paired) {
                            continue;
                        }
                        double source_x = source.anchor_x / from_layout.width;
                        double source_y = source.anchor_y / from_layout.height;
                        double target_x =
                            to_layout.layers[to_index].anchor_x / to_layout.width;
                        double target_y =
                            to_layout.layers[to_index].anchor_y / to_layout.height;
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

        if (!matching_ready) {
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

        if (!matching_ready) {
            Frame blended(from_frame.width, from_frame.height);
            blended.clear(0, 0, 0, 0);
            blend_frames(from_frame, to_frame, blended, smoothstep(clamped));
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

        double eased = smoothstep(clamped);
        double outgoing_opacity = 1.0 - smoothstep(clamped / 0.55);
        double incoming_opacity = smoothstep((clamped - 0.45) / 0.55);
        int drift = std::max(3, static_cast<int>(std::lround(from_frame.height * 0.08)));
        int from_padding_x = (from_frame.width - from_content_width) / 2;
        int from_padding_y = (from_frame.height - from_content_height) / 2;
        int to_padding_x = (to_frame.width - to_content_width) / 2;
        int to_padding_y = (to_frame.height - to_content_height) / 2;

        for (const GlyphLayer &source : from_layers) {
            int layer_x = base_x + from_padding_x;
            int layer_y = base_y + from_padding_y;
            if (source.match_index >= 0) {
                const GlyphLayer &destination = to_layers[source.match_index];
                double source_anchor_x =
                    from_padding_x + source.anchor_x * raster_scale;
                double source_anchor_y =
                    from_padding_y + source.anchor_y * raster_scale;
                double target_anchor_x =
                    to_padding_x + destination.anchor_x * raster_scale;
                double target_anchor_y =
                    to_padding_y + destination.anchor_y * raster_scale;
                layer_x += static_cast<int>(
                    std::lround((target_anchor_x - source_anchor_x) * eased));
                layer_y += static_cast<int>(
                    std::lround((target_anchor_y - source_anchor_y) * eased));
                alpha_over(source.frame,
                           target,
                           layer_x,
                           layer_y,
                           1.0,
                           tint_set,
                           tint_r,
                           tint_g,
                           tint_b);
            } else if (source.pair_index >= 0) {
                const GlyphLayer &destination = to_layers[source.pair_index];
                double source_anchor_x =
                    from_padding_x + source.anchor_x * raster_scale;
                double source_anchor_y =
                    from_padding_y + source.anchor_y * raster_scale;
                double target_anchor_x =
                    to_padding_x + destination.anchor_x * raster_scale;
                double target_anchor_y =
                    to_padding_y + destination.anchor_y * raster_scale;
                double offset_x = target_anchor_x - source_anchor_x;
                double offset_y = target_anchor_y - source_anchor_y;

                layer_x += static_cast<int>(std::lround(offset_x * eased));
                layer_y += static_cast<int>(std::lround(offset_y * eased));
                alpha_over(source.frame,
                           target,
                           layer_x,
                           layer_y,
                           1.0 - eased,
                           tint_set,
                           tint_r,
                           tint_g,
                           tint_b);

                int destination_x = base_x + to_padding_x -
                                    static_cast<int>(
                                        std::lround(offset_x * (1.0 - eased)));
                int destination_y = base_y + to_padding_y -
                                    static_cast<int>(
                                        std::lround(offset_y * (1.0 - eased)));
                alpha_over(destination.frame,
                           target,
                           destination_x,
                           destination_y,
                           eased,
                           tint_set,
                           tint_r,
                           tint_g,
                           tint_b);
            } else {
                layer_y -= static_cast<int>(std::lround(drift * eased));
                alpha_over(source.frame,
                           target,
                           layer_x,
                           layer_y,
                           outgoing_opacity,
                           tint_set,
                           tint_r,
                           tint_g,
                           tint_b);
            }
        }

        for (const GlyphLayer &destination : to_layers) {
            if (destination.matched || destination.paired)
                continue;
            int layer_x = base_x + to_padding_x;
            int layer_y = base_y + to_padding_y +
                          static_cast<int>(std::lround(drift * (1.0 - eased)));
            alpha_over(destination.frame,
                       target,
                       layer_x,
                       layer_y,
                       incoming_opacity,
                       tint_set,
                       tint_r,
                       tint_g,
                       tint_b);
        }
    }

} // namespace panim
