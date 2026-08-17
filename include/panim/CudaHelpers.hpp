// Backward-compatible wrapper for the old hardware-only invert API.
#pragma once

#include "Compute.hpp"

namespace panim {

    inline bool gpu_invert(Frame &frame) {
        ComputeParams params;
        params.allow_cpu_fallback = false;
        ComputeResult result =
            apply_compute_effect(frame, ComputeEffect::Invert, params);
        return result.ok && result.hardware_accelerated;
    }

} // namespace panim
