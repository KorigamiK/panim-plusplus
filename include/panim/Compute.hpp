// Cross-platform pixel compute abstraction with CPU fallback.
#pragma once

#include <cstdint>
#include <string>

#include "Frame.hpp"

namespace panim {

    enum class ComputeBackend : uint8_t {
        Cpu,
        WebGpu,
    };

    enum class ComputeEffect : uint32_t {
        Invert = 0,
        AnimatedGradient = 1,
        Mandelbulb = 2,
    };

    struct ComputeParams {
        float time_seconds = 0.0f;
        float strength = 1.0f;
        bool allow_cpu_fallback = true;
    };

    struct ComputeDeviceInfo {
        ComputeBackend backend = ComputeBackend::Cpu;
        std::string backend_name = "CPU";
        std::string device_name = "Portable CPU fallback";
        bool hardware_accelerated = false;
    };

    struct ComputeResult {
        bool ok = false;
        ComputeBackend backend = ComputeBackend::Cpu;
        bool hardware_accelerated = false;
        std::string message;
    };

    // Selection is automatic and happens once per process. The environment
    // overrides are intended only for testing and diagnostics:
    // PANIM_COMPUTE_BACKEND=webgpu|cpu and WGPU_BACKEND=metal|vulkan|dx12|gl.
    const ComputeDeviceInfo &compute_device();

    // Applies the selected backend. If it fails and fallback is allowed, the
    // operation is retried with the portable CPU implementation.
    ComputeResult apply_compute_effect(Frame &frame,
                                       ComputeEffect effect,
                                       const ComputeParams &params = {});

    const char *compute_backend_name(ComputeBackend backend);

} // namespace panim
