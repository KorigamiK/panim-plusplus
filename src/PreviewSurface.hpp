#pragma once

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

#include "panim/Status.hpp"

namespace panim::detail {

    struct PreviewSurface {
        WGPUSurface surface = nullptr;
        void *platform_view = nullptr;
    };

    Status create_preview_surface(WGPUInstance instance, SDL_Window *window, PreviewSurface &result);
    void destroy_preview_surface(PreviewSurface &surface);

} // namespace panim::detail
