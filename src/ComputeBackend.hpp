#pragma once

#include <string>

#include "panim/Compute.hpp"

namespace panim::detail {

#ifdef PANIM_HAVE_WEBGPU
    bool webgpu_backend_available(std::string &device_name,
                                  std::string &api_name,
                                  bool &hardware_accelerated,
                                  std::string &error);
    bool webgpu_backend_apply(Frame &frame,
                              ComputeEffect effect,
                              const ComputeParams &params,
                              std::string &error);
#endif

} // namespace panim::detail
