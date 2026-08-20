#pragma once

#include <string>

#include "panim/Compute.hpp"

namespace panim::detail {

    bool webgpu_backend_available(std::string &device_name, std::string &api_name, std::string &error);
    bool webgpu_backend_apply(Frame &frame, ComputeEffect effect, const ComputeParams &params, std::string &error);

} // namespace panim::detail
