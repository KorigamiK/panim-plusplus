#include "Preview.hpp"
#include "PreviewSurface.hpp"
#include "Shaders.hpp"

#include <SDL3/SDL.h>
#include <cairo/cairo.h>
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "panim/Animation.hpp"
#include "panim/FrameSink.hpp"
#include "panim/LatexRenderer.hpp"
#include "panim/Log.hpp"
#include "panim/PluginHost.hpp"
#include "panim/RenderSession.hpp"

namespace panim {

    namespace {

        struct AdapterRequest {
            WGPUAdapter adapter = nullptr;
            WGPURequestAdapterStatus status = WGPURequestAdapterStatus_Error;
            std::string message;
        };

        struct DeviceRequest {
            WGPUDevice device = nullptr;
            WGPURequestDeviceStatus status = WGPURequestDeviceStatus_Error;
            std::string message;
        };

        std::string string_from_view(WGPUStringView view) {
            if (!view.data)
                return {};
            if (view.length == WGPU_STRLEN)
                return view.data;
            return {view.data, view.length};
        }

        WGPUStringView string_view(const char *text) { return {text, WGPU_STRLEN}; }

        void handle_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *userdata, void *) {
            auto *request = static_cast<AdapterRequest *>(userdata);
            request->status = status;
            request->adapter = adapter;
            request->message = string_from_view(message);
        }

        void handle_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void *userdata, void *) {
            auto *request = static_cast<DeviceRequest *>(userdata);
            request->status = status;
            request->device = device;
            request->message = string_from_view(message);
        }

        void handle_uncaptured_error(WGPUDevice const *, WGPUErrorType, WGPUStringView message, void *, void *) {
            PANIM_LOG_ERROR("WebGPU preview validation error: {}", string_from_view(message));
        }

        const char *backend_name(WGPUBackendType backend) {
            switch (backend) {
            case WGPUBackendType_D3D11:
                return "D3D11";
            case WGPUBackendType_D3D12:
                return "D3D12";
            case WGPUBackendType_Metal:
                return "Metal";
            case WGPUBackendType_OpenGL:
                return "OpenGL";
            case WGPUBackendType_OpenGLES:
                return "OpenGL ES";
            case WGPUBackendType_Vulkan:
                return "Vulkan";
            case WGPUBackendType_WebGPU:
                return "Browser WebGPU";
            case WGPUBackendType_Null:
                return "Null";
            case WGPUBackendType_Undefined:
            case WGPUBackendType_Force32:
                break;
            }
            return "Unknown API";
        }

        const char *surface_status_name(WGPUSurfaceGetCurrentTextureStatus status) {
            if (static_cast<uint32_t>(status) == static_cast<uint32_t>(WGPUSurfaceGetCurrentTextureStatus_Occluded)) {
                return "occluded";
            }
            switch (status) {
            case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
                return "success";
            case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
                return "suboptimal";
            case WGPUSurfaceGetCurrentTextureStatus_Timeout:
                return "timeout";
            case WGPUSurfaceGetCurrentTextureStatus_Outdated:
                return "outdated";
            case WGPUSurfaceGetCurrentTextureStatus_Lost:
                return "lost";
            case WGPUSurfaceGetCurrentTextureStatus_Error:
                return "error";
            case WGPUSurfaceGetCurrentTextureStatus_Force32:
                break;
            }
            return "unknown";
        }

        constexpr double osc_virtual_height = 720.0;
        constexpr double osc_bar_height = 54.0;
        constexpr double osc_min_width = 854.0;
        constexpr double osc_line_1 = 12.0;
        constexpr double osc_line_2 = 39.0;
        constexpr double osc_button_width = 27.0;
        constexpr double osc_padding = 9.0;
        constexpr double osc_time_width = 110.0;

        double osc_scale(int width, int height) {
            double scale = static_cast<double>(height) / osc_virtual_height;
            if (static_cast<double>(width) / scale < osc_min_width)
                scale = static_cast<double>(width) / osc_min_width;
            return scale;
        }

        double osc_width(int width, int height) {
            return static_cast<double>(width) / osc_scale(width, height);
        }

        std::pair<double, double> osc_seek_bounds(double virtual_width) {
            double play_x = -2.0 + osc_padding + osc_button_width * 0.5;
            double next_x = play_x + (osc_button_width + osc_padding) * 2.0;
            double left_time_right = next_x + osc_button_width * 0.5 +
                                     osc_padding + osc_time_width;
            double right_time_right = virtual_width - osc_padding;
            return {
                left_time_right + osc_padding,
                right_time_right - osc_time_width - osc_padding,
            };
        }

        std::string format_osc_time(double seconds, bool negative) {
            auto total = static_cast<long long>(std::floor(std::max(seconds, 0.0)));
            long long hours = total / 3600;
            long long minutes = (total / 60) % 60;
            long long remaining_seconds = total % 60;
            char text[32]{};
            std::snprintf(text,
                          sizeof(text),
                          negative ? "-%02lld:%02lld:%02lld" : "%02lld:%02lld:%02lld",
                          hours,
                          minutes,
                          remaining_seconds);
            return text;
        }

        enum class TextAlignment {
            Left,
            Right,
        };

        void draw_osc_text(cairo_t *cr,
                           const std::string &text,
                           double x,
                           double center_y,
                           TextAlignment alignment) {
            cairo_text_extents_t extents{};
            cairo_text_extents(cr, text.c_str(), &extents);
            double text_x = x - extents.x_bearing;
            if (alignment == TextAlignment::Right)
                text_x -= extents.width;
            double text_y = center_y - extents.y_bearing - extents.height * 0.5;
            cairo_move_to(cr, text_x, text_y);
            cairo_show_text(cr, text.c_str());
        }

        void draw_play_pause(cairo_t *cr, double center_x, double center_y, bool playing) {
            if (playing) {
                cairo_rectangle(cr, center_x - 5.5, center_y - 8.0, 4.0, 16.0);
                cairo_rectangle(cr, center_x + 1.5, center_y - 8.0, 4.0, 16.0);
                cairo_fill(cr);
                return;
            }
            cairo_move_to(cr, center_x - 5.0, center_y - 9.0);
            cairo_line_to(cr, center_x + 7.0, center_y);
            cairo_line_to(cr, center_x - 5.0, center_y + 9.0);
            cairo_close_path(cr);
            cairo_fill(cr);
        }

        void draw_chapter_button(cairo_t *cr,
                                 double center_x,
                                 double center_y,
                                 bool forward) {
            double direction = forward ? 1.0 : -1.0;
            cairo_move_to(cr, center_x - 6.0 * direction, center_y - 8.0);
            cairo_line_to(cr, center_x + 4.0 * direction, center_y);
            cairo_line_to(cr, center_x - 6.0 * direction, center_y + 8.0);
            cairo_close_path(cr);
            cairo_fill(cr);
            cairo_rectangle(cr,
                            center_x + 5.0 * direction - (forward ? 1.5 : 0.0),
                            center_y - 8.0,
                            1.5,
                            16.0);
            cairo_fill(cr);
        }

        void blend_osc(std::vector<uint8_t> &destination,
                       int width,
                       int start_y,
                       const std::vector<uint8_t> &overlay,
                       int overlay_height) {
            for (int y = 0; y < overlay_height; ++y) {
                for (int x = 0; x < width; ++x) {
                    size_t source_index =
                        (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 4;
                    uint32_t source_alpha = overlay[source_index + 3];
                    if (source_alpha == 0)
                        continue;
                    size_t destination_index =
                        (static_cast<size_t>(start_y + y) * width +
                         static_cast<size_t>(x)) * 4;
                    uint32_t inverse_alpha = 255 - source_alpha;
                    destination[destination_index] = static_cast<uint8_t>(
                        overlay[source_index + 2] +
                        (destination[destination_index] * inverse_alpha + 127) / 255);
                    destination[destination_index + 1] = static_cast<uint8_t>(
                        overlay[source_index + 1] +
                        (destination[destination_index + 1] * inverse_alpha + 127) / 255);
                    destination[destination_index + 2] = static_cast<uint8_t>(
                        overlay[source_index] +
                        (destination[destination_index + 2] * inverse_alpha + 127) / 255);
                    destination[destination_index + 3] = static_cast<uint8_t>(
                        source_alpha +
                        (destination[destination_index + 3] * inverse_alpha + 127) / 255);
                }
            }
        }

        void render_osc(std::vector<uint8_t> &destination,
                        std::vector<uint8_t> &overlay,
                        int width,
                        int height,
                        const std::string &title,
                        double time_seconds,
                        double duration_seconds,
                        bool playing,
                        double opacity) {
            double scale = osc_scale(width, height);
            int overlay_height = std::min(
                height, std::max(1, static_cast<int>(std::ceil(osc_bar_height * scale))));
            int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
            overlay.assign(static_cast<size_t>(stride) * overlay_height, 0);
            cairo_surface_t *surface = cairo_image_surface_create_for_data(
                overlay.data(), CAIRO_FORMAT_ARGB32, width, overlay_height, stride);
            if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
                cairo_surface_destroy(surface);
                return;
            }
            cairo_t *cr = cairo_create(surface);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
            cairo_font_options_t *font_options = cairo_font_options_create();
            cairo_font_options_set_antialias(font_options, CAIRO_ANTIALIAS_GRAY);
            cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_SLIGHT);
            cairo_set_font_options(cr, font_options);
            cairo_font_options_destroy(font_options);
            cairo_scale(cr, scale, scale);

            double virtual_width = static_cast<double>(width) / scale;
            double visible_height = static_cast<double>(overlay_height) / scale;
            cairo_push_group(cr);
            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 175.0 / 255.0);
            cairo_rectangle(cr, 0.0, 0.0, virtual_width, visible_height);
            cairo_fill(cr);

            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_select_font_face(cr,
                                   "sans-serif",
                                   CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 18.0);
            cairo_save(cr);
            cairo_rectangle(cr,
                            osc_padding,
                            0.0,
                            std::max(0.0, virtual_width - osc_padding * 2.0),
                            27.0);
            cairo_clip(cr);
            draw_osc_text(cr, title, osc_padding, osc_line_1, TextAlignment::Left);
            cairo_restore(cr);

            double play_x = -2.0 + osc_padding + osc_button_width * 0.5;
            double previous_x = play_x + osc_button_width + osc_padding;
            double next_x = previous_x + osc_button_width + osc_padding;
            draw_play_pause(cr, play_x, osc_line_2, playing);
            draw_chapter_button(cr, previous_x, osc_line_2, false);
            draw_chapter_button(cr, next_x, osc_line_2, true);

            auto [seek_left, seek_right] = osc_seek_bounds(virtual_width);
            double left_time_right = seek_left - osc_padding;
            double right_time_right = virtual_width - osc_padding;
            double seek_height = 25.0;
            double seek_top = osc_line_2 - seek_height * 0.5;
            if (seek_right > seek_left) {
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 35.0 / 255.0);
                cairo_rectangle(cr,
                                seek_left,
                                seek_top,
                                seek_right - seek_left,
                                seek_height);
                cairo_fill(cr);
                double progress = std::clamp(time_seconds / duration_seconds, 0.0, 1.0);
                cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
                cairo_rectangle(cr,
                                seek_left,
                                seek_top,
                                (seek_right - seek_left) * progress,
                                seek_height);
                cairo_fill(cr);
            }

            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_set_font_size(cr, 27.0);
            draw_osc_text(cr,
                          format_osc_time(time_seconds, false),
                          left_time_right,
                          osc_line_2,
                          TextAlignment::Right);
            draw_osc_text(cr,
                          format_osc_time(duration_seconds - time_seconds, true),
                          right_time_right,
                          osc_line_2,
                          TextAlignment::Right);

            cairo_pop_group_to_source(cr);
            cairo_paint_with_alpha(cr, std::clamp(opacity, 0.0, 1.0));
            cairo_surface_flush(surface);
            cairo_destroy(cr);
            cairo_surface_destroy(surface);
            blend_osc(destination,
                      width,
                      height - overlay_height,
                      overlay,
                      overlay_height);
        }

        class PreviewPresenter {
        public:
            ~PreviewPresenter() {
                release_frame_texture();
                if (sampler_)
                    wgpuSamplerRelease(sampler_);
                if (bind_group_layout_)
                    wgpuBindGroupLayoutRelease(bind_group_layout_);
                if (pipeline_)
                    wgpuRenderPipelineRelease(pipeline_);
                if (shader_)
                    wgpuShaderModuleRelease(shader_);
                if (queue_)
                    wgpuQueueRelease(queue_);
                if (device_)
                    wgpuDeviceRelease(device_);
                if (adapter_)
                    wgpuAdapterRelease(adapter_);
                detail::destroy_preview_surface(surface_);
                if (instance_)
                    wgpuInstanceRelease(instance_);
            }

            Status initialize(SDL_Window *window) {
                window_ = window;
                instance_ = wgpuCreateInstance(nullptr);
                if (!instance_)
                    return Status::failure("Failed to create WebGPU instance");

                Status status = detail::create_preview_surface(instance_, window_, surface_);
                if (!status.ok)
                    return status;

                AdapterRequest adapter_request;
                WGPURequestAdapterOptions adapter_options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
                adapter_options.compatibleSurface = surface_.surface;
                WGPURequestAdapterCallbackInfo adapter_callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
                adapter_callback.mode = WGPUCallbackMode_AllowSpontaneous;
                adapter_callback.callback = handle_adapter;
                adapter_callback.userdata1 = &adapter_request;
                wgpuInstanceRequestAdapter(instance_, &adapter_options, adapter_callback);
                if (adapter_request.status != WGPURequestAdapterStatus_Success || !adapter_request.adapter) {
                    return Status::failure("Failed to request preview adapter: " + adapter_request.message);
                }
                adapter_ = adapter_request.adapter;

                DeviceRequest device_request;
                WGPURequestDeviceCallbackInfo device_callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
                device_callback.mode = WGPUCallbackMode_AllowSpontaneous;
                device_callback.callback = handle_device;
                device_callback.userdata1 = &device_request;
                WGPUDeviceDescriptor device_descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
                device_descriptor.label = string_view("panim preview device");
                device_descriptor.uncapturedErrorCallbackInfo.callback = handle_uncaptured_error;
                wgpuAdapterRequestDevice(adapter_, &device_descriptor, device_callback);
                if (device_request.status != WGPURequestDeviceStatus_Success || !device_request.device) {
                    return Status::failure("Failed to request preview device: " + device_request.message);
                }
                device_ = device_request.device;
                queue_ = wgpuDeviceGetQueue(device_);
                if (!queue_)
                    return Status::failure("Failed to get preview queue");

                WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
                if (wgpuAdapterGetInfo(adapter_, &info) == WGPUStatus_Success) {
                    std::string device_name = string_from_view(info.device);
                    if (device_name.empty())
                        device_name = string_from_view(info.description);
                    PANIM_LOG_INFO("Preview GPU: {} via {}", device_name.empty() ? "WebGPU adapter" : device_name, backend_name(info.backendType));
                    wgpuAdapterInfoFreeMembers(info);
                }

                status = choose_surface_format();
                if (!status.ok)
                    return status;
                status = create_pipeline();
                if (!status.ok)
                    return status;
                return configure_surface();
            }

            Status present(const Frame &frame,
                           const std::string &title,
                           double time_seconds,
                           double duration_seconds,
                           bool playing,
                           double controls_opacity) {
                Status status = ensure_frame_texture(frame.width, frame.height);
                if (!status.ok)
                    return status;
                status = configure_surface();
                if (!status.ok)
                    return status;

                WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                destination.texture = frame_texture_;
                WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
                layout.bytesPerRow = static_cast<uint32_t>(frame.width * 4);
                layout.rowsPerImage = static_cast<uint32_t>(frame.height);
                WGPUExtent3D extent{
                    static_cast<uint32_t>(frame.width),
                    static_cast<uint32_t>(frame.height),
                    1,
                };
                const std::vector<uint8_t> *pixels = &frame.pixels;
                if (controls_opacity > 0.001) {
                    preview_pixels_ = frame.pixels;
                    render_osc(preview_pixels_,
                               overlay_pixels_,
                               frame.width,
                               frame.height,
                               title,
                               time_seconds,
                               duration_seconds,
                               playing,
                               controls_opacity);
                    pixels = &preview_pixels_;
                }
                wgpuQueueWriteTexture(queue_,
                                      &destination,
                                      pixels->data(),
                                      pixels->size(),
                                      &layout,
                                      &extent);

                WGPUSurfaceTexture surface_texture = WGPU_SURFACE_TEXTURE_INIT;
                wgpuSurfaceGetCurrentTexture(surface_.surface, &surface_texture);
                if (surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated ||
                    surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Lost) {
                    if (surface_texture.texture)
                        wgpuTextureRelease(surface_texture.texture);
                    configured_width_ = 0;
                    configured_height_ = 0;
                    return configure_surface();
                }
                if (surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Timeout) {
                    return Status::success();
                }
                if (static_cast<uint32_t>(surface_texture.status) == static_cast<uint32_t>(WGPUSurfaceGetCurrentTextureStatus_Occluded)) {
                    return Status::success();
                }
                const bool suboptimal = surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
                if (surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal && !suboptimal) {
                    if (surface_texture.texture)
                        wgpuTextureRelease(surface_texture.texture);
                    return Status::failure("Failed to acquire a WebGPU surface texture (" + std::string(surface_status_name(surface_texture.status)) +
                                           ", code " + std::to_string(static_cast<uint32_t>(surface_texture.status)) + ")");
                }

                WGPUTextureView surface_view = wgpuTextureCreateView(surface_texture.texture, nullptr);
                if (!surface_view) {
                    wgpuTextureRelease(surface_texture.texture);
                    return Status::failure("Failed to create preview surface view");
                }

                WGPUCommandEncoderDescriptor encoder_descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
                WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encoder_descriptor);
                WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                attachment.view = surface_view;
                attachment.loadOp = WGPULoadOp_Clear;
                attachment.storeOp = WGPUStoreOp_Store;
                attachment.clearValue = {0.015, 0.02, 0.03, 1.0};
                WGPURenderPassDescriptor pass_descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                pass_descriptor.colorAttachmentCount = 1;
                pass_descriptor.colorAttachments = &attachment;
                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);

                const double scale =
                    std::min(static_cast<double>(configured_width_) / frame.width, static_cast<double>(configured_height_) / frame.height);
                const float viewport_width = static_cast<float>(frame.width * scale);
                const float viewport_height = static_cast<float>(frame.height * scale);
                const float viewport_x = (configured_width_ - viewport_width) * 0.5f;
                const float viewport_y = (configured_height_ - viewport_height) * 0.5f;
                wgpuRenderPassEncoderSetViewport(pass, viewport_x, viewport_y, viewport_width, viewport_height, 0.0f, 1.0f);
                wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group_, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);

                WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, nullptr);
                wgpuCommandEncoderRelease(encoder);
                if (!command) {
                    wgpuTextureViewRelease(surface_view);
                    wgpuTextureRelease(surface_texture.texture);
                    return Status::failure("Failed to encode preview commands");
                }
                wgpuQueueSubmit(queue_, 1, &command);
                wgpuCommandBufferRelease(command);
                WGPUStatus present_status = wgpuSurfacePresent(surface_.surface);
                wgpuTextureViewRelease(surface_view);
                wgpuTextureRelease(surface_texture.texture);
                if (present_status != WGPUStatus_Success)
                    return Status::failure("WebGPU surface present failed");

                if (suboptimal) {
                    configured_width_ = 0;
                    configured_height_ = 0;
                }
                return Status::success();
            }

        private:
            Status choose_surface_format() {
                WGPUSurfaceCapabilities capabilities = WGPU_SURFACE_CAPABILITIES_INIT;
                if (wgpuSurfaceGetCapabilities(surface_.surface, adapter_, &capabilities) != WGPUStatus_Success || capabilities.formatCount == 0) {
                    wgpuSurfaceCapabilitiesFreeMembers(capabilities);
                    return Status::failure("Preview surface has no supported formats");
                }

                surface_format_ = capabilities.formats[0];
                if (capabilities.alphaModeCount > 0)
                    alpha_mode_ = capabilities.alphaModes[0];
                for (WGPUTextureFormat candidate : {
                         WGPUTextureFormat_BGRA8UnormSrgb,
                         WGPUTextureFormat_RGBA8UnormSrgb,
                         WGPUTextureFormat_BGRA8Unorm,
                         WGPUTextureFormat_RGBA8Unorm,
                     }) {
                    for (size_t index = 0; index < capabilities.formatCount; ++index) {
                        if (capabilities.formats[index] == candidate) {
                            surface_format_ = candidate;
                            wgpuSurfaceCapabilitiesFreeMembers(capabilities);
                            return Status::success();
                        }
                    }
                }
                wgpuSurfaceCapabilitiesFreeMembers(capabilities);
                return Status::success();
            }

            Status create_pipeline() {
                WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
                source.code = {detail::preview_shader_wgsl,
                               sizeof(detail::preview_shader_wgsl) - 1};
                WGPUShaderModuleDescriptor shader_descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
                shader_descriptor.nextInChain = &source.chain;
                shader_descriptor.label = string_view("panim preview WGSL");
                shader_ = wgpuDeviceCreateShaderModule(device_, &shader_descriptor);

                WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
                target.format = surface_format_;
                WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
                fragment.module = shader_;
                fragment.entryPoint = string_view("fragment_main");
                fragment.targetCount = 1;
                fragment.targets = &target;
                WGPURenderPipelineDescriptor pipeline_descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
                pipeline_descriptor.label = string_view("panim preview pipeline");
                pipeline_descriptor.vertex.module = shader_;
                pipeline_descriptor.vertex.entryPoint = string_view("vertex_main");
                pipeline_descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
                pipeline_descriptor.fragment = &fragment;
                pipeline_ = wgpuDeviceCreateRenderPipeline(device_, &pipeline_descriptor);
                if (!shader_ || !pipeline_)
                    return Status::failure("Failed to create preview WGSL pipeline");

                bind_group_layout_ = wgpuRenderPipelineGetBindGroupLayout(pipeline_, 0);
                WGPUSamplerDescriptor sampler_descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
                sampler_descriptor.magFilter = WGPUFilterMode_Linear;
                sampler_descriptor.minFilter = WGPUFilterMode_Linear;
                sampler_ = wgpuDeviceCreateSampler(device_, &sampler_descriptor);
                if (!bind_group_layout_ || !sampler_)
                    return Status::failure("Failed to create preview pipeline resources");
                return Status::success();
            }

            Status configure_surface() {
                int width = 0;
                int height = 0;
                if (!SDL_GetWindowSizeInPixels(window_, &width, &height)) {
                    return Status::failure("Could not query preview window size: " + std::string(SDL_GetError()));
                }
                if (width <= 0 || height <= 0)
                    return Status::success();
                if (width == configured_width_ && height == configured_height_) {
                    return Status::success();
                }

                WGPUSurfaceConfiguration configuration = WGPU_SURFACE_CONFIGURATION_INIT;
                configuration.device = device_;
                configuration.format = surface_format_;
                configuration.usage = WGPUTextureUsage_RenderAttachment;
                configuration.width = static_cast<uint32_t>(width);
                configuration.height = static_cast<uint32_t>(height);
                configuration.presentMode = WGPUPresentMode_Fifo;
                configuration.alphaMode = alpha_mode_;
                wgpuSurfaceConfigure(surface_.surface, &configuration);
                configured_width_ = width;
                configured_height_ = height;
                return Status::success();
            }

            Status ensure_frame_texture(int width, int height) {
                if (frame_texture_ && width == frame_width_ && height == frame_height_) {
                    return Status::success();
                }
                release_frame_texture();

                WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
                descriptor.label = string_view("panim preview frame");
                descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
                descriptor.dimension = WGPUTextureDimension_2D;
                descriptor.size = {
                    static_cast<uint32_t>(width),
                    static_cast<uint32_t>(height),
                    1,
                };
                descriptor.format = WGPUTextureFormat_RGBA8UnormSrgb;
                frame_texture_ = wgpuDeviceCreateTexture(device_, &descriptor);
                if (frame_texture_)
                    frame_view_ = wgpuTextureCreateView(frame_texture_, nullptr);

                WGPUBindGroupEntry entries[2]{WGPU_BIND_GROUP_ENTRY_INIT,
                                              WGPU_BIND_GROUP_ENTRY_INIT};
                entries[0].binding = 0;
                entries[0].textureView = frame_view_;
                entries[1].binding = 1;
                entries[1].sampler = sampler_;
                WGPUBindGroupDescriptor bind_descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
                bind_descriptor.label = string_view("panim preview frame resources");
                bind_descriptor.layout = bind_group_layout_;
                bind_descriptor.entryCount = 2;
                bind_descriptor.entries = entries;
                if (frame_view_) {
                    bind_group_ = wgpuDeviceCreateBindGroup(device_, &bind_descriptor);
                }
                if (!frame_texture_ || !frame_view_ || !bind_group_) {
                    release_frame_texture();
                    return Status::failure("Failed to create preview frame texture");
                }
                frame_width_ = width;
                frame_height_ = height;
                return Status::success();
            }

            void release_frame_texture() {
                if (bind_group_)
                    wgpuBindGroupRelease(bind_group_);
                if (frame_view_)
                    wgpuTextureViewRelease(frame_view_);
                if (frame_texture_)
                    wgpuTextureRelease(frame_texture_);
                bind_group_ = nullptr;
                frame_view_ = nullptr;
                frame_texture_ = nullptr;
                frame_width_ = 0;
                frame_height_ = 0;
            }

            SDL_Window *window_ = nullptr;
            WGPUInstance instance_ = nullptr;
            detail::PreviewSurface surface_;
            WGPUAdapter adapter_ = nullptr;
            WGPUDevice device_ = nullptr;
            WGPUQueue queue_ = nullptr;
            WGPUShaderModule shader_ = nullptr;
            WGPURenderPipeline pipeline_ = nullptr;
            WGPUBindGroupLayout bind_group_layout_ = nullptr;
            WGPUSampler sampler_ = nullptr;
            WGPUTexture frame_texture_ = nullptr;
            WGPUTextureView frame_view_ = nullptr;
            WGPUBindGroup bind_group_ = nullptr;
            WGPUTextureFormat surface_format_ = WGPUTextureFormat_Undefined;
            WGPUCompositeAlphaMode alpha_mode_ = WGPUCompositeAlphaMode_Auto;
            int configured_width_ = 0;
            int configured_height_ = 0;
            int frame_width_ = 0;
            int frame_height_ = 0;
            std::vector<uint8_t> preview_pixels_;
            std::vector<uint8_t> overlay_pixels_;
        };

        using AnimationPtr = std::unique_ptr<Animation, std::function<void(Animation *)>>;

        struct LoadedAnimation {
            LoadedAnimation(std::unique_ptr<PluginHost> host_value, AnimationPtr animation_value, std::unique_ptr<RenderSession> session_value,
                            std::string name_value, double duration_value, double fps_value)
                : host(std::move(host_value)), animation(std::move(animation_value)), session(std::move(session_value)), name(std::move(name_value)),
                  duration(duration_value), fps(fps_value) {}

            std::unique_ptr<PluginHost> host;
            AnimationPtr animation;
            std::unique_ptr<RenderSession> session;
            std::string name;
            double duration = 1.0;
            double fps = 30.0;
        };

        std::unique_ptr<LoadedAnimation> load_animation(const std::filesystem::path &path, const PreviewOptions &options, LatexRenderer *latex,
                                                        Status &status) {
            auto host = std::make_unique<PluginHost>(path);
            if (!host->valid()) {
                status = Status::failure("Plugin load failed: " + host->status().message);
                return nullptr;
            }
            AnimationPtr animation = host->create();
            if (!animation) {
                status = Status::failure("Plugin did not create an animation instance");
                return nullptr;
            }

            AnimationInfo info = animation->info();
            RenderSessionOptions session_options;
            session_options.width = options.width.value_or(info.width);
            session_options.height = options.height.value_or(info.height);
            session_options.fps = options.fps.value_or(info.fps);
            session_options.duration = options.duration.value_or(info.duration);
            session_options.latex = latex;
            session_options.output_dir = options.output_dir;
            if (session_options.width <= 0 || session_options.height <= 0 || session_options.fps <= 0.0 || session_options.duration <= 0.0) {
                status = Status::failure("Plugin returned invalid preview settings");
                return nullptr;
            }

            auto session = std::make_unique<RenderSession>(*animation, session_options);
            status = session->setup();
            if (!status.ok)
                return nullptr;

            std::string name = info.name ? info.name : "Untitled";
            return std::make_unique<LoadedAnimation>(std::move(host), std::move(animation), std::move(session), std::move(name),
                                                     session_options.duration, session_options.fps);
        }

        class TemporaryDirectory {
        public:
            Status create() {
                std::error_code error;
                std::filesystem::path base = std::filesystem::temp_directory_path(error);
                if (error) {
                    return Status::failure("Could not locate the temporary directory: " + error.message());
                }
                auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
                path_ = base / ("panim-preview-" + std::to_string(nonce));
                std::filesystem::create_directories(path_, error);
                if (error) {
                    return Status::failure("Could not create preview reload directory: " + error.message());
                }
                return Status::success();
            }

            ~TemporaryDirectory() {
                if (path_.empty())
                    return;
                std::error_code error;
                std::filesystem::remove_all(path_, error);
            }

            Status copy_plugin(const std::filesystem::path &source, uint64_t generation, std::filesystem::path &destination) const {
                destination = path_ / ("plugin-" + std::to_string(generation) + source.extension().string());
                std::error_code error;
                std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
                if (error) {
                    return Status::failure("Could not stage plugin for reload: " + error.message());
                }
                return Status::success();
            }

        private:
            std::filesystem::path path_;
        };

        std::string safe_stem(std::string name) {
            for (char &character : name) {
                bool valid = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                             (character >= '0' && character <= '9') || character == '-' || character == '_';
                if (!valid)
                    character = '-';
            }
            return name.empty() ? "animation" : name;
        }

        Status save_screenshot(const LoadedAnimation &loaded, const std::filesystem::path &output_dir, double time_seconds) {
            std::error_code error;
            std::filesystem::create_directories(output_dir, error);
            if (error) {
                return Status::failure("Could not create screenshot directory: " + error.message());
            }
            long long milliseconds = static_cast<long long>(std::llround(time_seconds * 1000.0));
            std::filesystem::path path = output_dir / (safe_stem(loaded.name) + "-" + std::to_string(milliseconds) + "ms.png");
            const Frame &frame = loaded.session->frame();
            VideoWriterOptions writer_options;
            writer_options.input_width = frame.width;
            writer_options.input_height = frame.height;
            VideoFrameSink sink(path, frame.width, frame.height, loaded.fps, writer_options);
            if (!sink.ok())
                return sink.status();
            Status status = sink.submit(frame, 0, time_seconds);
            if (status.ok)
                status = sink.finish();
            if (status.ok)
                PANIM_LOG_INFO("Saved lossless screenshot: {}", path.string());
            return status;
        }

        enum class PreviewControl {
            None = 0,
            PlayPause = 1,
            StepBack = 2,
            StepForward = 3,
            Timeline = 4,
        };

        struct PreviewPoint {
            double x = 0.0;
            double y = 0.0;
            bool inside = false;
        };

        PreviewPoint preview_point(SDL_Window *window, int frame_width, int frame_height, float mouse_x, float mouse_y) {
            int window_width = 0;
            int window_height = 0;
            if (!SDL_GetWindowSize(window, &window_width, &window_height) || window_width <= 0 || window_height <= 0 || frame_width <= 0 ||
                frame_height <= 0) {
                return {};
            }

            double scale = std::min(static_cast<double>(window_width) / frame_width, static_cast<double>(window_height) / frame_height);
            double viewport_width = frame_width * scale;
            double viewport_height = frame_height * scale;
            double viewport_x = (window_width - viewport_width) * 0.5;
            double viewport_y = (window_height - viewport_height) * 0.5;
            PreviewPoint point;
            point.x = (mouse_x - viewport_x) / viewport_width;
            point.y = (mouse_y - viewport_y) / viewport_height;
            point.inside = point.x >= 0.0 && point.x <= 1.0 && point.y >= 0.0 && point.y <= 1.0;
            return point;
        }

        PreviewControl control_at(const PreviewPoint &point,
                                  int frame_width,
                                  int frame_height) {
            if (!point.inside)
                return PreviewControl::None;

            double scale = osc_scale(frame_width, frame_height);
            double virtual_x = point.x * frame_width / scale;
            double from_bottom = (1.0 - point.y) * frame_height / scale;
            if (from_bottom > osc_bar_height)
                return PreviewControl::None;

            double play_x = -2.0 + osc_padding + osc_button_width * 0.5;
            double centers[] = {
                play_x,
                play_x + osc_button_width + osc_padding,
                play_x + (osc_button_width + osc_padding) * 2.0,
            };
            constexpr PreviewControl controls[] = {
                PreviewControl::PlayPause,
                PreviewControl::StepBack,
                PreviewControl::StepForward,
            };
            for (size_t index = 0; index < std::size(centers); ++index) {
                if (std::abs(virtual_x - centers[index]) <= osc_button_width * 0.5 &&
                    std::abs(from_bottom - (osc_bar_height - osc_line_2)) <= 14.5) {
                    return controls[index];
                }
            }
            auto [seek_left, seek_right] = osc_seek_bounds(osc_width(frame_width, frame_height));
            if (virtual_x >= seek_left && virtual_x <= seek_right &&
                std::abs(from_bottom - (osc_bar_height - osc_line_2)) <= 14.5) {
                return PreviewControl::Timeline;
            }
            return PreviewControl::None;
        }

    } // namespace

    Status run_preview(const PreviewOptions &options) {
        std::error_code directory_error;
        std::filesystem::create_directories(options.output_dir, directory_error);
        if (directory_error) {
            return Status::failure("Could not create preview output directory: " + directory_error.message());
        }

        TemporaryDirectory reload_directory;
        Status status = reload_directory.create();
        if (!status.ok)
            return status;
        uint64_t generation = 1;
        std::filesystem::path staged_plugin;
        status = reload_directory.copy_plugin(options.plugin_path, generation, staged_plugin);
        if (!status.ok)
            return status;

        LatexRenderer latex(options.output_dir / "latex");
        if (!latex.available())
            PANIM_LOG_WARN("LaTeX disabled: {}", latex.last_error());
        auto loaded = load_animation(staged_plugin, options, latex.available() ? &latex : nullptr, status);
        if (!loaded)
            return status;

        if (!SDL_Init(SDL_INIT_VIDEO)) {
            return Status::failure("SDL video initialization failed: " + std::string(SDL_GetError()));
        }

        const int source_width = loaded->session->options().width;
        const int source_height = loaded->session->options().height;
        const double window_scale = std::min({1.0, 1280.0 / source_width, 720.0 / source_height});
        const int window_width = std::max(320, static_cast<int>(std::lround(source_width * window_scale)));
        const int window_height = std::max(180, static_cast<int>(std::lround(source_height * window_scale)));
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#ifdef __APPLE__
        flags |= SDL_WINDOW_METAL;
#endif
        SDL_Window *window = SDL_CreateWindow("panim++", window_width, window_height, flags);
        if (!window) {
            std::string message = SDL_GetError();
            SDL_Quit();
            return Status::failure("Could not create preview window: " + message);
        }
        SDL_SyncWindow(window);
        SDL_RaiseWindow(window);

        auto presenter = std::make_unique<PreviewPresenter>();
        status = presenter->initialize(window);
        if (!status.ok) {
            presenter.reset();
            SDL_DestroyWindow(window);
            SDL_Quit();
            return status;
        }
        SDL_Cursor *pointer_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);

        PANIM_LOG_INFO("Interactive preview: {}x{} @ {} fps", source_width, source_height, loaded->fps);
        PANIM_LOG_INFO("Controls: mpv-style bottom bar, Space play/pause, "
                       "Left/Right step, Shift step 1s, S screenshot, R reload, Esc quit");
        if (options.watch_plugin) {
            PANIM_LOG_INFO("Watching plugin: {}", options.plugin_path.string());
        }

        std::filesystem::file_time_type observed_write_time{};
        std::uintmax_t observed_size = 0;
        std::error_code watch_error;
        observed_write_time = std::filesystem::last_write_time(options.plugin_path, watch_error);
        watch_error.clear();
        observed_size = std::filesystem::file_size(options.plugin_path, watch_error);
        watch_error.clear();

        using Clock = std::chrono::steady_clock;
        auto previous_tick = Clock::now();
        auto previous_watch = previous_tick;
        auto pending_since = previous_tick;
        auto next_frame = previous_tick;
        constexpr auto controls_hold = std::chrono::milliseconds(500);
        constexpr auto controls_fade = std::chrono::milliseconds(200);
        auto last_pointer_activity = previous_tick - controls_hold - controls_fade;
        bool reload_pending = false;
        bool force_reload = false;
        bool running = true;
        bool playing = true;
        bool dirty = true;
        bool scrubbing = false;
        bool mouse_inside = true;
        bool cursor_visible = SDL_CursorVisible();
        PreviewControl hovered_control = PreviewControl::None;
        Status loop_status = Status::success();
        double time_seconds = std::clamp(options.start_time, 0.0, loaded->duration);
        double controls_opacity = 0.0;
        int presented_frames = 0;

        auto point_from_mouse = [&](float mouse_x, float mouse_y) { return preview_point(window, source_width, source_height, mouse_x, mouse_y); };
        auto seek_from_mouse = [&](float mouse_x, float mouse_y) {
            PreviewPoint point = point_from_mouse(mouse_x, mouse_y);
            if (!point.inside)
                return;
            double scale = osc_scale(source_width, source_height);
            double virtual_x = point.x * source_width / scale;
            auto [seek_left, seek_right] =
                osc_seek_bounds(osc_width(source_width, source_height));
            double ratio = std::clamp(
                (virtual_x - seek_left) / (seek_right - seek_left), 0.0, 1.0);
            time_seconds = ratio * loaded->duration;
            playing = false;
            dirty = true;
        };
        auto capture_screenshot = [&]() {
            Status screenshot_status = loaded->session->render_at(time_seconds);
            if (screenshot_status.ok) {
                screenshot_status = save_screenshot(*loaded, options.output_dir, time_seconds);
            }
            if (!screenshot_status.ok) {
                PANIM_LOG_ERROR("Screenshot failed: {}", screenshot_status.message);
            }
            dirty = true;
        };
        auto note_pointer_activity = [&]() {
            last_pointer_activity = Clock::now();
            if (!cursor_visible) {
                SDL_ShowCursor();
                cursor_visible = true;
            }
            dirty = true;
        };
        auto set_hovered_control = [&](PreviewControl control) {
            if (hovered_control == control)
                return;
            hovered_control = control;
            if (pointer_cursor) {
                SDL_SetCursor(control == PreviewControl::None ? SDL_GetDefaultCursor() : pointer_cursor);
            }
            dirty = true;
        };

        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    running = false;
                } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_EXPOSED ||
                           event.type == SDL_EVENT_WINDOW_RESTORED || event.type == SDL_EVENT_WINDOW_SHOWN) {
                    dirty = true;
                } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    switch (event.key.key) {
                    case SDLK_ESCAPE:
                        running = false;
                        break;
                    case SDLK_SPACE:
                        playing = !playing;
                        dirty = true;
                        break;
                    case SDLK_LEFT: {
                        double step = (event.key.mod & SDL_KMOD_SHIFT) ? 1.0 : 1.0 / loaded->fps;
                        time_seconds = std::max(0.0, time_seconds - step);
                        playing = false;
                        dirty = true;
                        break;
                    }
                    case SDLK_RIGHT: {
                        double step = (event.key.mod & SDL_KMOD_SHIFT) ? 1.0 : 1.0 / loaded->fps;
                        time_seconds = std::min(loaded->duration, time_seconds + step);
                        playing = false;
                        dirty = true;
                        break;
                    }
                    case SDLK_HOME:
                        time_seconds = 0.0;
                        playing = false;
                        dirty = true;
                        break;
                    case SDLK_END:
                        time_seconds = loaded->duration;
                        playing = false;
                        dirty = true;
                        break;
                    case SDLK_S:
                        capture_screenshot();
                        break;
                    case SDLK_R:
                        force_reload = true;
                        break;
                    default:
                        break;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                    note_pointer_activity();
                    PreviewControl control = control_at(
                        point_from_mouse(event.button.x, event.button.y),
                        source_width,
                        source_height);
                    set_hovered_control(control);
                    switch (control) {
                    case PreviewControl::PlayPause:
                        playing = !playing;
                        dirty = true;
                        break;
                    case PreviewControl::StepBack:
                        time_seconds = std::max(0.0, time_seconds - 1.0 / loaded->fps);
                        playing = false;
                        dirty = true;
                        break;
                    case PreviewControl::StepForward:
                        time_seconds = std::min(loaded->duration,
                                                time_seconds + 1.0 / loaded->fps);
                        playing = false;
                        dirty = true;
                        break;
                    case PreviewControl::Timeline:
                        scrubbing = true;
                        seek_from_mouse(event.button.x, event.button.y);
                        break;
                    case PreviewControl::None:
                        break;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
                    note_pointer_activity();
                    scrubbing = false;
                } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                    mouse_inside = true;
                    note_pointer_activity();
                    set_hovered_control(control_at(
                        point_from_mouse(event.motion.x, event.motion.y),
                        source_width,
                        source_height));
                    if (scrubbing)
                        seek_from_mouse(event.motion.x, event.motion.y);
                } else if (event.type == SDL_EVENT_WINDOW_MOUSE_ENTER) {
                    mouse_inside = true;
                } else if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
                    mouse_inside = false;
                    if (!cursor_visible) {
                        SDL_ShowCursor();
                        cursor_visible = true;
                    }
                    set_hovered_control(PreviewControl::None);
                }
            }

            auto now = Clock::now();
            const double elapsed = std::chrono::duration<double>(now - previous_tick).count();
            previous_tick = now;
            if (playing) {
                time_seconds += elapsed;
                if (time_seconds > loaded->duration) {
                    time_seconds = std::fmod(time_seconds, loaded->duration);
                }
                dirty = true;
            }

            double next_controls_opacity = 1.0;
            if (!scrubbing && now - last_pointer_activity > controls_hold) {
                auto fade_elapsed = now - last_pointer_activity - controls_hold;
                next_controls_opacity = 1.0 -
                    std::chrono::duration<double>(fade_elapsed).count() /
                        std::chrono::duration<double>(controls_fade).count();
                next_controls_opacity = std::clamp(next_controls_opacity, 0.0, 1.0);
            }
            if (std::abs(next_controls_opacity - controls_opacity) > 0.001) {
                controls_opacity = next_controls_opacity;
                dirty = true;
            }
            if (controls_opacity <= 0.001 && hovered_control != PreviewControl::None)
                set_hovered_control(PreviewControl::None);

            const bool should_show_cursor = !mouse_inside || scrubbing || controls_opacity > 0.001;
            if (should_show_cursor != cursor_visible) {
                if (should_show_cursor)
                    SDL_ShowCursor();
                else
                    SDL_HideCursor();
                cursor_visible = should_show_cursor;
            }

            if (options.watch_plugin && now - previous_watch >= std::chrono::milliseconds(200)) {
                previous_watch = now;
                std::error_code error;
                auto write_time = std::filesystem::last_write_time(options.plugin_path, error);
                std::uintmax_t file_size = error ? 0 : std::filesystem::file_size(options.plugin_path, error);
                if (!error && (write_time != observed_write_time || file_size != observed_size)) {
                    observed_write_time = write_time;
                    observed_size = file_size;
                    reload_pending = true;
                    pending_since = now;
                }
            }

            if (force_reload || (reload_pending && now - pending_since >= std::chrono::milliseconds(300))) {
                force_reload = false;
                reload_pending = false;
                std::filesystem::path reload_path;
                status = reload_directory.copy_plugin(options.plugin_path, ++generation, reload_path);
                if (status.ok) {
                    Status load_status;
                    auto replacement = load_animation(reload_path, options, latex.available() ? &latex : nullptr, load_status);
                    if (replacement) {
                        loaded = std::move(replacement);
                        time_seconds = std::clamp(time_seconds, 0.0, loaded->duration);
                        PANIM_LOG_INFO("Reloaded plugin generation {} at {:.2f} s", generation, time_seconds);
                        dirty = true;
                    } else {
                        PANIM_LOG_ERROR("Reload rejected; keeping previous animation: {}", load_status.message);
                    }
                } else {
                    PANIM_LOG_ERROR("Reload staging failed; keeping previous animation: {}", status.message);
                }
            }

            const bool frame_due = now >= next_frame;
            if (dirty && frame_due) {
                status = loaded->session->render_at(time_seconds);
                if (!status.ok) {
                    PANIM_LOG_ERROR("Preview render failed: {}", status.message);
                    loop_status = status;
                    running = false;
                    continue;
                }
                status = presenter->present(loaded->session->frame(),
                                            loaded->name,
                                            time_seconds,
                                            loaded->duration,
                                            playing,
                                            controls_opacity);
                if (!status.ok) {
                    PANIM_LOG_ERROR("Preview presentation failed: {}", status.message);
                    loop_status = status;
                    running = false;
                    continue;
                }
                dirty = false;
                next_frame = now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / loaded->fps));
                ++presented_frames;
                if (options.frame_limit && presented_frames >= *options.frame_limit) {
                    running = false;
                }
            }
            SDL_Delay(1);
        }

        presenter.reset();
        SDL_ShowCursor();
        if (pointer_cursor) {
            SDL_SetCursor(SDL_GetDefaultCursor());
            SDL_DestroyCursor(pointer_cursor);
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        return loop_status;
    }

} // namespace panim
