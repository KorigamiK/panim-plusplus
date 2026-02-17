// Optional CUDA helpers for simple per-frame processing.
#pragma once

#include "Frame.hpp"

namespace panim {

#ifdef PANIM_ENABLE_CUDA
    bool gpu_invert(Frame &frame);
#else
    inline bool gpu_invert(Frame &) { return false; }
#endif

} // namespace panim
