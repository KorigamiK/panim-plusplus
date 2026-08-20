#include "panim/Compute.hpp"
#include "panim/Log.hpp"

#include "ComputeBackend.hpp"

#include <utility>

namespace panim {

    namespace {

        struct ComputeRuntime {
            ComputeDeviceInfo info;
            bool initialized = false;
            bool available = false;
            bool failure_logged = false;
            std::string error;
        };

        ComputeRuntime &runtime() {
            static ComputeRuntime instance;
            return instance;
        }

        void initialize_runtime() {
            ComputeRuntime &state = runtime();
            if (state.initialized)
                return;

            state.initialized = true;
            state.available = detail::webgpu_backend_available(state.info.device_name, state.info.backend_name, state.error);
            if (state.available) {
                state.info.backend_name = "WebGPU / " + state.info.backend_name;
                PANIM_LOG_INFO("Compute device: {} ({})", state.info.backend_name, state.info.device_name);
            } else {
                PANIM_LOG_ERROR("WebGPU compute unavailable: {}", state.error);
            }
        }

    } // namespace

    const ComputeDeviceInfo &compute_device() {
        initialize_runtime();
        return runtime().info;
    }

    ComputeResult apply_compute_effect(Frame &frame, ComputeEffect effect, const ComputeParams &params) {
        initialize_runtime();
        ComputeRuntime &state = runtime();
        if (!state.available)
            return {false, state.error};

        std::string error;
        if (detail::webgpu_backend_apply(frame, effect, params, error))
            return {true, {}};

        if (!state.failure_logged) {
            PANIM_LOG_ERROR("WebGPU compute failed: {}", error);
            state.failure_logged = true;
        }
        return {false, std::move(error)};
    }

} // namespace panim
