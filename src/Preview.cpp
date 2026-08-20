#include "Preview.hpp"
#include "PreviewSurface.hpp"

#include <SDL3/SDL.h>
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

#include "panim/Animation.hpp"
#include "panim/FrameSink.hpp"
#include "panim/LatexRenderer.hpp"
#include "panim/Log.hpp"
#include "panim/PluginHost.hpp"
#include "panim/RenderSession.hpp"

namespace panim {

    namespace {

        constexpr const char preview_shader[] = R"(
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

struct PreviewParams {
    progress: f32,
    playing: f32,
    hovered_control: f32,
    padding: f32,
};

@group(0) @binding(0) var frame_texture: texture_2d<f32>;
@group(0) @binding(1) var frame_sampler: sampler;
@group(0) @binding(2) var<uniform> params: PreviewParams;

@vertex
fn vertex_main(@builtin(vertex_index) index: u32) -> VertexOutput {
    let positions = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>(3.0, -1.0),
        vec2<f32>(-1.0, 3.0)
    );
    var output: VertexOutput;
    let position = positions[index];
    output.position = vec4<f32>(position, 0.0, 1.0);
    output.uv = position * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5);
    return output;
}

fn inside_box(point: vec2<f32>, center: vec2<f32>, half_size: vec2<f32>) -> bool {
    let distance = abs(point - center);
    return distance.x <= half_size.x && distance.y <= half_size.y;
}

fn cross_2d(a: vec2<f32>, b: vec2<f32>) -> f32 {
    return a.x * b.y - a.y * b.x;
}

fn inside_triangle(
    point: vec2<f32>,
    a: vec2<f32>,
    b: vec2<f32>,
    c: vec2<f32>
) -> bool {
    let side_a = cross_2d(b - a, point - a);
    let side_b = cross_2d(c - b, point - b);
    let side_c = cross_2d(a - c, point - c);
    let has_negative = side_a < 0.0 || side_b < 0.0 || side_c < 0.0;
    let has_positive = side_a > 0.0 || side_b > 0.0 || side_c > 0.0;
    return !(has_negative && has_positive);
}

fn is_hovered(control: f32) -> bool {
    return abs(params.hovered_control - control) < 0.25;
}

@fragment
fn fragment_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let frame = textureSample(frame_texture, frame_sampler, input.uv);
    if (input.uv.y < 0.86) {
        return frame;
    }

    let toolbar = vec3<f32>(0.025, 0.035, 0.055);
    let button = vec3<f32>(0.10, 0.13, 0.18);
    let hover = vec3<f32>(0.18, 0.24, 0.32);
    let icon = vec3<f32>(0.92, 0.95, 1.0);
    let muted = vec3<f32>(0.20, 0.25, 0.33);
    let paused = vec3<f32>(0.98, 0.70, 0.24);
    let running = vec3<f32>(0.16, 0.82, 0.78);
    let active_color = select(paused, running, params.playing > 0.5);
    var color = mix(frame.rgb, toolbar, 0.94);

    let centers = array<f32, 6>(0.050, 0.115, 0.185, 0.255, 0.855, 0.925);
    var control = 0u;
    loop {
        if (control >= 6u) {
            break;
        }
        let control_id = f32(control + 1u);
        if (inside_box(input.uv,
                       vec2<f32>(centers[control], 0.93),
                       vec2<f32>(0.027, 0.048))) {
            color = select(button, hover, is_hovered(control_id));
        }
        control += 1u;
    }

    let restart_local = vec2<f32>((input.uv.x - 0.050) / 0.022,
                                  (input.uv.y - 0.93) / 0.038);
    let restart_bar = abs(restart_local.x + 0.52) < 0.10 &&
                      abs(restart_local.y) < 0.52;
    let restart_triangle = inside_triangle(restart_local,
                                           vec2<f32>(-0.28, -0.55),
                                           vec2<f32>(0.58, 0.0),
                                           vec2<f32>(-0.28, 0.55));
    if (restart_bar || restart_triangle) {
        color = icon;
    }

    let previous_local = vec2<f32>((input.uv.x - 0.115) / 0.021,
                                   (input.uv.y - 0.93) / 0.038);
    let previous_triangle = inside_triangle(previous_local,
                                            vec2<f32>(0.48, -0.56),
                                            vec2<f32>(-0.48, 0.0),
                                            vec2<f32>(0.48, 0.56));
    if (previous_triangle) {
        color = icon;
    }

    let play_local = vec2<f32>((input.uv.x - 0.185) / 0.022,
                               (input.uv.y - 0.93) / 0.038);
    let play_triangle = inside_triangle(play_local,
                                        vec2<f32>(-0.38, -0.62),
                                        vec2<f32>(0.58, 0.0),
                                        vec2<f32>(-0.38, 0.62));
    let pause_bars = (abs(play_local.x - 0.28) < 0.14 ||
                      abs(play_local.x + 0.28) < 0.14) &&
                     abs(play_local.y) < 0.58;
    if (select(play_triangle, pause_bars, params.playing > 0.5)) {
        color = active_color;
    }

    let next_local = vec2<f32>((input.uv.x - 0.255) / 0.021,
                               (input.uv.y - 0.93) / 0.038);
    let next_triangle = inside_triangle(next_local,
                                        vec2<f32>(-0.48, -0.56),
                                        vec2<f32>(0.48, 0.0),
                                        vec2<f32>(-0.48, 0.56));
    if (next_triangle) {
        color = icon;
    }

    let timeline_start = 0.30;
    let timeline_end = 0.80;
    let timeline_position = mix(timeline_start, timeline_end, params.progress);
    if (input.uv.x >= timeline_start && input.uv.x <= timeline_end &&
        abs(input.uv.y - 0.93) < 0.010) {
        color = select(muted, active_color, input.uv.x <= timeline_position);
    }
    let knob = vec2<f32>((input.uv.x - timeline_position) / 0.008,
                         (input.uv.y - 0.93) / 0.014);
    if (length(knob) <= 1.0) {
        color = active_color;
    }

    let camera_local = vec2<f32>((input.uv.x - 0.855) / 0.022,
                                 (input.uv.y - 0.93) / 0.038);
    let camera_outer = abs(camera_local.x) < 0.62 &&
                       abs(camera_local.y - 0.08) < 0.46;
    let camera_inner = abs(camera_local.x) < 0.48 &&
                       abs(camera_local.y - 0.04) < 0.30;
    let camera_top = abs(camera_local.x + 0.22) < 0.25 &&
                     abs(camera_local.y + 0.48) < 0.12;
    let camera_lens = length(vec2<f32>(camera_local.x / 0.30,
                                       camera_local.y / 0.42)) < 0.62;
    if ((camera_outer && !camera_inner) || camera_top || camera_lens) {
        color = icon;
    }

    let reload_local = vec2<f32>((input.uv.x - 0.925) / 0.022,
                                 (input.uv.y - 0.93) / 0.038);
    let reload_radius = length(vec2<f32>(reload_local.x,
                                         reload_local.y * 0.80));
    let reload_ring = reload_radius > 0.38 && reload_radius < 0.59 &&
                      !(reload_local.x > 0.20 && reload_local.y < -0.20);
    let reload_arrow = inside_triangle(reload_local,
                                        vec2<f32>(0.02, -0.58),
                                        vec2<f32>(0.68, -0.68),
                                        vec2<f32>(0.50, -0.05));
    if (reload_ring || reload_arrow) {
        color = icon;
    }

    return vec4<f32>(color, 1.0);
}
)";

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

        struct PreviewParams {
            float progress = 0.0f;
            float playing = 1.0f;
            float hovered_control = 0.0f;
            float padding = 0.0f;
        };

        static_assert(sizeof(PreviewParams) == 16);

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

        class PreviewPresenter {
        public:
            ~PreviewPresenter() {
                release_frame_texture();
                if (params_buffer_)
                    wgpuBufferRelease(params_buffer_);
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

            Status present(const Frame &frame, double progress, bool playing, int hovered_control) {
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
                wgpuQueueWriteTexture(queue_, &destination, frame.pixels.data(), frame.pixels.size(), &layout, &extent);

                PreviewParams params;
                params.progress = static_cast<float>(std::clamp(progress, 0.0, 1.0));
                params.playing = playing ? 1.0f : 0.0f;
                params.hovered_control = static_cast<float>(hovered_control);
                wgpuQueueWriteBuffer(queue_, params_buffer_, 0, &params, sizeof(params));

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
                source.code = {preview_shader, sizeof(preview_shader) - 1};
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
                WGPUBufferDescriptor buffer_descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
                buffer_descriptor.label = string_view("panim preview timeline parameters");
                buffer_descriptor.size = sizeof(PreviewParams);
                buffer_descriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
                params_buffer_ = wgpuDeviceCreateBuffer(device_, &buffer_descriptor);
                if (!bind_group_layout_ || !sampler_ || !params_buffer_)
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

                WGPUBindGroupEntry entries[3]{WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
                entries[0].binding = 0;
                entries[0].textureView = frame_view_;
                entries[1].binding = 1;
                entries[1].sampler = sampler_;
                entries[2].binding = 2;
                entries[2].buffer = params_buffer_;
                entries[2].size = sizeof(PreviewParams);
                WGPUBindGroupDescriptor bind_descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
                bind_descriptor.label = string_view("panim preview frame resources");
                bind_descriptor.layout = bind_group_layout_;
                bind_descriptor.entryCount = 3;
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
            WGPUBuffer params_buffer_ = nullptr;
            WGPUTexture frame_texture_ = nullptr;
            WGPUTextureView frame_view_ = nullptr;
            WGPUBindGroup bind_group_ = nullptr;
            WGPUTextureFormat surface_format_ = WGPUTextureFormat_Undefined;
            WGPUCompositeAlphaMode alpha_mode_ = WGPUCompositeAlphaMode_Auto;
            int configured_width_ = 0;
            int configured_height_ = 0;
            int frame_width_ = 0;
            int frame_height_ = 0;
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
            Restart = 1,
            StepBack = 2,
            PlayPause = 3,
            StepForward = 4,
            Screenshot = 5,
            Reload = 6,
            Timeline = 7,
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

        PreviewControl control_at(const PreviewPoint &point) {
            if (!point.inside || point.y < 0.86)
                return PreviewControl::None;

            constexpr double centers[] = {
                0.050, 0.115, 0.185, 0.255, 0.855, 0.925,
            };
            constexpr PreviewControl controls[] = {
                PreviewControl::Restart,     PreviewControl::StepBack,   PreviewControl::PlayPause,
                PreviewControl::StepForward, PreviewControl::Screenshot, PreviewControl::Reload,
            };
            for (size_t index = 0; index < std::size(centers); ++index) {
                if (std::abs(point.x - centers[index]) <= 0.030 && std::abs(point.y - 0.93) <= 0.060) {
                    return controls[index];
                }
            }
            if (point.x >= 0.29 && point.x <= 0.81)
                return PreviewControl::Timeline;
            return PreviewControl::None;
        }

        const char *control_label(PreviewControl control) {
            switch (control) {
            case PreviewControl::Restart:
                return "Restart";
            case PreviewControl::StepBack:
                return "Previous frame";
            case PreviewControl::PlayPause:
                return "Play / pause";
            case PreviewControl::StepForward:
                return "Next frame";
            case PreviewControl::Screenshot:
                return "Save PNG";
            case PreviewControl::Reload:
                return "Reload plugin";
            case PreviewControl::Timeline:
                return "Scrub timeline";
            case PreviewControl::None:
                break;
            }
            return "";
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
        PANIM_LOG_INFO("Controls: clickable transport, Space play/pause, Left/Right step, "
                       "Shift step 1s, S screenshot, R reload, Esc quit");
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
        bool reload_pending = false;
        bool force_reload = false;
        bool running = true;
        bool playing = true;
        bool dirty = true;
        bool scrubbing = false;
        PreviewControl hovered_control = PreviewControl::None;
        Status loop_status = Status::success();
        double time_seconds = std::clamp(options.start_time, 0.0, loaded->duration);
        int presented_frames = 0;

        auto point_from_mouse = [&](float mouse_x, float mouse_y) { return preview_point(window, source_width, source_height, mouse_x, mouse_y); };
        auto seek_from_mouse = [&](float mouse_x, float mouse_y) {
            PreviewPoint point = point_from_mouse(mouse_x, mouse_y);
            if (!point.inside)
                return;
            double ratio = std::clamp((point.x - 0.30) / (0.80 - 0.30), 0.0, 1.0);
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
                    PreviewControl control = control_at(point_from_mouse(event.button.x, event.button.y));
                    set_hovered_control(control);
                    switch (control) {
                    case PreviewControl::Restart:
                        time_seconds = 0.0;
                        playing = false;
                        dirty = true;
                        break;
                    case PreviewControl::StepBack:
                        time_seconds = std::max(0.0, time_seconds - 1.0 / loaded->fps);
                        playing = false;
                        dirty = true;
                        break;
                    case PreviewControl::PlayPause:
                        playing = !playing;
                        dirty = true;
                        break;
                    case PreviewControl::StepForward:
                        time_seconds = std::min(loaded->duration, time_seconds + 1.0 / loaded->fps);
                        playing = false;
                        dirty = true;
                        break;
                    case PreviewControl::Screenshot:
                        capture_screenshot();
                        break;
                    case PreviewControl::Reload:
                        force_reload = true;
                        break;
                    case PreviewControl::Timeline:
                        scrubbing = true;
                        seek_from_mouse(event.button.x, event.button.y);
                        break;
                    case PreviewControl::None:
                        break;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
                    scrubbing = false;
                } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                    set_hovered_control(control_at(point_from_mouse(event.motion.x, event.motion.y)));
                    if (scrubbing)
                        seek_from_mouse(event.motion.x, event.motion.y);
                } else if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
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

            const bool frame_due = !playing || now >= next_frame;
            if (dirty && frame_due) {
                status = loaded->session->render_at(time_seconds);
                if (!status.ok) {
                    PANIM_LOG_ERROR("Preview render failed: {}", status.message);
                    loop_status = status;
                    running = false;
                    continue;
                }
                status = presenter->present(loaded->session->frame(), time_seconds / loaded->duration, playing, static_cast<int>(hovered_control));
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
        if (pointer_cursor) {
            SDL_SetCursor(SDL_GetDefaultCursor());
            SDL_DestroyCursor(pointer_cursor);
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        return loop_status;
    }

} // namespace panim
