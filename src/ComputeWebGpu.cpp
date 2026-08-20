#include "ComputeBackend.hpp"
#include "ComputeShader.hpp"

#include "panim/Log.hpp"

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace panim::detail {

    namespace {

        struct ShaderParams {
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t effect = 0;
            float time_seconds = 0.0f;
            float strength = 1.0f;
            uint32_t padding[3]{};
        };

        static_assert(sizeof(ShaderParams) == 32);

        std::string string_from_view(WGPUStringView view) {
            if (!view.data)
                return {};
            if (view.length == WGPU_STRLEN)
                return view.data;
            return {view.data, view.length};
        }

        WGPUStringView string_view(const char *text) { return {text, WGPU_STRLEN}; }

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

        struct WebGpuState {
            WGPUInstance instance = nullptr;
            WGPUAdapter adapter = nullptr;
            WGPUDevice device = nullptr;
            WGPUQueue queue = nullptr;
            WGPUShaderModule shader = nullptr;
            WGPUComputePipeline pipeline = nullptr;
            WGPUBindGroupLayout bind_group_layout = nullptr;
            WGPUBindGroup bind_group = nullptr;
            WGPUBuffer storage_buffer = nullptr;
            WGPUBuffer readback_buffer = nullptr;
            WGPUBuffer params_buffer = nullptr;
            uint64_t buffer_capacity = 0;
            std::string device_name;
            std::string api_name;
            std::string error;
            bool hardware_accelerated = false;
            bool attempted = false;

            ~WebGpuState() {
                if (bind_group)
                    wgpuBindGroupRelease(bind_group);
                if (storage_buffer)
                    wgpuBufferRelease(storage_buffer);
                if (readback_buffer)
                    wgpuBufferRelease(readback_buffer);
                if (params_buffer)
                    wgpuBufferRelease(params_buffer);
                if (bind_group_layout)
                    wgpuBindGroupLayoutRelease(bind_group_layout);
                if (pipeline)
                    wgpuComputePipelineRelease(pipeline);
                if (shader)
                    wgpuShaderModuleRelease(shader);
                if (queue)
                    wgpuQueueRelease(queue);
                if (device)
                    wgpuDeviceRelease(device);
                if (adapter)
                    wgpuAdapterRelease(adapter);
                if (instance)
                    wgpuInstanceRelease(instance);
            }
        };

        WebGpuState &webgpu_state() {
            static WebGpuState state;
            return state;
        }

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

        struct MapRequest {
            WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
            std::string message;
        };

        struct ErrorScopeRequest {
            WGPUPopErrorScopeStatus status = WGPUPopErrorScopeStatus_Error;
            WGPUErrorType type = WGPUErrorType_NoError;
            std::string message;
            bool completed = false;
        };

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

        void handle_map(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata, void *) {
            auto *request = static_cast<MapRequest *>(userdata);
            request->status = status;
            request->message = string_from_view(message);
        }

        void handle_error_scope(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message, void *userdata, void *) {
            auto *request = static_cast<ErrorScopeRequest *>(userdata);
            request->status = status;
            request->type = type;
            request->message = string_from_view(message);
            request->completed = true;
        }

        bool pop_error_scope(WebGpuState &state, std::string_view context, std::string &error) {
            ErrorScopeRequest request;
            WGPUPopErrorScopeCallbackInfo callback = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
            callback.mode = WGPUCallbackMode_AllowSpontaneous;
            callback.callback = handle_error_scope;
            callback.userdata1 = &request;
            wgpuDevicePopErrorScope(state.device, callback);
            wgpuDevicePoll(state.device, WGPU_TRUE, nullptr);

            if (!request.completed || request.status != WGPUPopErrorScopeStatus_Success) {
                error = std::string(context) + " validation did not complete: " + request.message;
                return false;
            }
            if (request.type != WGPUErrorType_NoError) {
                error = std::string(context) + " failed: " + request.message;
                return false;
            }
            return true;
        }

        void handle_wgpu_log(WGPULogLevel level, WGPUStringView message, void *) {
            std::string text = string_from_view(message);
            if (level == WGPULogLevel_Error) {
                PANIM_LOG_ERROR("WebGPU: {}", text);
            } else if (level == WGPULogLevel_Warn) {
                PANIM_LOG_WARN("WebGPU: {}", text);
            }
        }

        void handle_uncaptured_error(WGPUDevice const *, WGPUErrorType, WGPUStringView message, void *, void *) {
            PANIM_LOG_ERROR("WebGPU validation error: {}", string_from_view(message));
        }

        bool initialize_webgpu() {
            WebGpuState &state = webgpu_state();
            if (state.attempted)
                return state.pipeline != nullptr;
            state.attempted = true;

            wgpuSetLogCallback(handle_wgpu_log, nullptr);
            wgpuSetLogLevel(WGPULogLevel_Warn);

            state.instance = wgpuCreateInstance(nullptr);
            if (!state.instance) {
                state.error = "Failed to create a WebGPU instance";
                return false;
            }

            AdapterRequest adapter_request;
            WGPURequestAdapterCallbackInfo adapter_callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
            adapter_callback.mode = WGPUCallbackMode_AllowSpontaneous;
            adapter_callback.callback = handle_adapter;
            adapter_callback.userdata1 = &adapter_request;
            wgpuInstanceRequestAdapter(state.instance, nullptr, adapter_callback);
            if (adapter_request.status != WGPURequestAdapterStatus_Success || !adapter_request.adapter) {
                state.error = "Failed to request a WebGPU adapter: " + adapter_request.message;
                return false;
            }
            state.adapter = adapter_request.adapter;

            DeviceRequest device_request;
            WGPURequestDeviceCallbackInfo device_callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
            device_callback.mode = WGPUCallbackMode_AllowSpontaneous;
            device_callback.callback = handle_device;
            device_callback.userdata1 = &device_request;
            WGPUDeviceDescriptor device_descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
            device_descriptor.label = string_view("panim compute device");
            device_descriptor.uncapturedErrorCallbackInfo.callback = handle_uncaptured_error;
            wgpuAdapterRequestDevice(state.adapter, &device_descriptor, device_callback);
            if (device_request.status != WGPURequestDeviceStatus_Success || !device_request.device) {
                state.error = "Failed to request a WebGPU device: " + device_request.message;
                return false;
            }
            state.device = device_request.device;
            state.queue = wgpuDeviceGetQueue(state.device);
            if (!state.queue) {
                state.error = "Failed to get the WebGPU device queue";
                return false;
            }

            WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
            if (wgpuAdapterGetInfo(state.adapter, &info) == WGPUStatus_Success) {
                state.device_name = string_from_view(info.device);
                if (state.device_name.empty())
                    state.device_name = string_from_view(info.description);
                state.api_name = backend_name(info.backendType);
                state.hardware_accelerated = info.adapterType != WGPUAdapterType_CPU;
                wgpuAdapterInfoFreeMembers(info);
            }
            if (state.device_name.empty())
                state.device_name = "WebGPU adapter";
            if (state.api_name.empty())
                state.api_name = "Unknown API";

            wgpuDevicePushErrorScope(state.device, WGPUErrorFilter_Validation);

            WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
            source.code = {compute_shader_wgsl, sizeof(compute_shader_wgsl) - 1};
            WGPUShaderModuleDescriptor shader_descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
            shader_descriptor.label = string_view("panim compute WGSL");
            shader_descriptor.nextInChain = &source.chain;
            state.shader = wgpuDeviceCreateShaderModule(state.device, &shader_descriptor);

            if (state.shader) {
                WGPUComputePipelineDescriptor pipeline_descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
                pipeline_descriptor.label = string_view("panim compute pipeline");
                pipeline_descriptor.compute.module = state.shader;
                pipeline_descriptor.compute.entryPoint = string_view("panim_effect");
                state.pipeline = wgpuDeviceCreateComputePipeline(state.device, &pipeline_descriptor);
            }

            if (state.pipeline) {
                state.bind_group_layout = wgpuComputePipelineGetBindGroupLayout(state.pipeline, 0);
            }

            WGPUBufferDescriptor params_descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
            params_descriptor.label = string_view("panim compute parameters");
            params_descriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            params_descriptor.size = sizeof(ShaderParams);
            if (state.bind_group_layout) {
                state.params_buffer = wgpuDeviceCreateBuffer(state.device, &params_descriptor);
            }

            if (!pop_error_scope(state, "WebGPU shader/pipeline creation", state.error)) {
                return false;
            }
            if (!state.shader || !state.pipeline || !state.bind_group_layout || !state.params_buffer) {
                state.error = "WebGPU shader/pipeline creation returned an "
                              "empty resource";
                return false;
            }
            return true;
        }

        bool ensure_buffers(uint64_t byte_count, std::string &error) {
            WebGpuState &state = webgpu_state();
            if (state.bind_group && state.buffer_capacity >= byte_count)
                return true;

            if (state.bind_group) {
                wgpuBindGroupRelease(state.bind_group);
                state.bind_group = nullptr;
            }
            if (state.storage_buffer) {
                wgpuBufferRelease(state.storage_buffer);
                state.storage_buffer = nullptr;
            }
            if (state.readback_buffer) {
                wgpuBufferRelease(state.readback_buffer);
                state.readback_buffer = nullptr;
            }
            state.buffer_capacity = 0;

            WGPUBufferDescriptor storage_descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
            storage_descriptor.label = string_view("panim compute pixels");
            storage_descriptor.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
            storage_descriptor.size = byte_count;
            state.storage_buffer = wgpuDeviceCreateBuffer(state.device, &storage_descriptor);

            WGPUBufferDescriptor readback_descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
            readback_descriptor.label = string_view("panim compute readback");
            readback_descriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
            readback_descriptor.size = byte_count;
            state.readback_buffer = wgpuDeviceCreateBuffer(state.device, &readback_descriptor);
            if (!state.storage_buffer || !state.readback_buffer) {
                error = "Failed to allocate WebGPU frame buffers";
                return false;
            }

            WGPUBindGroupEntry entries[2]{WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
            entries[0].binding = 0;
            entries[0].buffer = state.storage_buffer;
            entries[0].size = byte_count;
            entries[1].binding = 1;
            entries[1].buffer = state.params_buffer;
            entries[1].size = sizeof(ShaderParams);

            WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            descriptor.label = string_view("panim compute resources");
            descriptor.layout = state.bind_group_layout;
            descriptor.entryCount = 2;
            descriptor.entries = entries;
            state.bind_group = wgpuDeviceCreateBindGroup(state.device, &descriptor);
            if (!state.bind_group) {
                error = "Failed to create the WebGPU bind group";
                return false;
            }
            state.buffer_capacity = byte_count;
            return true;
        }

    } // namespace

    bool webgpu_backend_available(std::string &device_name, std::string &api_name, std::string &error) {
        if (!initialize_webgpu()) {
            error = webgpu_state().error;
            return false;
        }
        if (!webgpu_state().hardware_accelerated) {
            error = "WebGPU selected a CPU adapter; a hardware GPU is required";
            return false;
        }
        device_name = webgpu_state().device_name;
        api_name = webgpu_state().api_name;
        return true;
    }

    bool webgpu_backend_apply(Frame &frame, ComputeEffect effect, const ComputeParams &params, std::string &error) {
        if (!initialize_webgpu()) {
            error = webgpu_state().error;
            return false;
        }
        if (frame.width <= 0 || frame.height <= 0)
            return true;

        uint64_t pixel_count = static_cast<uint64_t>(frame.width) * static_cast<uint64_t>(frame.height);
        if (pixel_count > std::numeric_limits<uint32_t>::max()) {
            error = "Frame is too large for the WebGPU compute shader";
            return false;
        }
        uint64_t byte_count = pixel_count * 4;
        if (byte_count != frame.pixels.size()) {
            error = "Frame storage does not match its dimensions";
            return false;
        }
        if (!ensure_buffers(byte_count, error))
            return false;

        WebGpuState &state = webgpu_state();
        ShaderParams shader_params;
        shader_params.width = static_cast<uint32_t>(frame.width);
        shader_params.height = static_cast<uint32_t>(frame.height);
        shader_params.effect = static_cast<uint32_t>(effect);
        shader_params.time_seconds = params.time_seconds;
        shader_params.strength = params.strength;
        wgpuQueueWriteBuffer(state.queue, state.storage_buffer, 0, frame.pixels.data(), frame.pixels.size());
        wgpuQueueWriteBuffer(state.queue, state.params_buffer, 0, &shader_params, sizeof(shader_params));

        WGPUCommandEncoderDescriptor encoder_descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
        encoder_descriptor.label = string_view("panim compute commands");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(state.device, &encoder_descriptor);
        if (!encoder) {
            error = "Failed to create a WebGPU command encoder";
            return false;
        }

        WGPUComputePassDescriptor pass_descriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        pass_descriptor.label = string_view("panim compute pass");
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &pass_descriptor);
        if (!pass) {
            wgpuCommandEncoderRelease(encoder);
            error = "Failed to create a WebGPU compute pass";
            return false;
        }
        wgpuComputePassEncoderSetPipeline(pass, state.pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, state.bind_group, 0, nullptr);
        constexpr uint32_t workgroup_size = 8;
        uint32_t workgroup_count_x = static_cast<uint32_t>((frame.width + workgroup_size - 1) / workgroup_size);
        uint32_t workgroup_count_y = static_cast<uint32_t>((frame.height + workgroup_size - 1) / workgroup_size);
        wgpuComputePassEncoderDispatchWorkgroups(pass, workgroup_count_x, workgroup_count_y, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        wgpuCommandEncoderCopyBufferToBuffer(encoder, state.storage_buffer, 0, state.readback_buffer, 0, byte_count);
        WGPUCommandBufferDescriptor command_descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
        command_descriptor.label = string_view("panim compute submission");
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &command_descriptor);
        wgpuCommandEncoderRelease(encoder);
        if (!command) {
            error = "Failed to finish the WebGPU command buffer";
            return false;
        }
        wgpuQueueSubmit(state.queue, 1, &command);
        wgpuCommandBufferRelease(command);

        MapRequest map_request;
        WGPUBufferMapCallbackInfo map_callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        map_callback.mode = WGPUCallbackMode_AllowSpontaneous;
        map_callback.callback = handle_map;
        map_callback.userdata1 = &map_request;
        wgpuBufferMapAsync(state.readback_buffer, WGPUMapMode_Read, 0, byte_count, map_callback);
        wgpuDevicePoll(state.device, WGPU_TRUE, nullptr);
        if (map_request.status != WGPUMapAsyncStatus_Success) {
            error = "Failed to map the WebGPU readback buffer: " + map_request.message;
            return false;
        }

        const void *mapped = wgpuBufferGetConstMappedRange(state.readback_buffer, 0, byte_count);
        if (!mapped) {
            wgpuBufferUnmap(state.readback_buffer);
            error = "WebGPU returned an empty mapped frame";
            return false;
        }
        std::memcpy(frame.pixels.data(), mapped, frame.pixels.size());
        wgpuBufferUnmap(state.readback_buffer);
        return true;
    }

} // namespace panim::detail
