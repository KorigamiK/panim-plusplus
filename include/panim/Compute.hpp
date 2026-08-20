// WebGPU pixel compute API.
#pragma once

#include <cstdint>
#include <string>

#include "Frame.hpp"

namespace panim {

    enum class ComputeEffect : uint32_t {
        Invert = 0,
        AnimatedGradient = 1,
        Mandelbulb = 2,
    };

    struct ComputeParams {
        float time_seconds = 0.0f;
        float strength = 1.0f;
    };

    struct ComputeDeviceInfo {
        std::string backend_name = "WebGPU";
        std::string device_name = "Unavailable";
    };

    struct ComputeResult {
        bool ok = false;
        std::string message;
    };

    // Adapter and native API selection are handled by WebGPU.
    const ComputeDeviceInfo &compute_device();

    ComputeResult apply_compute_effect(Frame &frame, ComputeEffect effect, const ComputeParams &params = {});

} // namespace panim
