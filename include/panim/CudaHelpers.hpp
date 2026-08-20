// Convenience wrapper for GPU inversion.
#pragma once

#include "Compute.hpp"

namespace panim {

    inline bool gpu_invert(Frame &frame) { return apply_compute_effect(frame, ComputeEffect::Invert).ok; }

} // namespace panim
