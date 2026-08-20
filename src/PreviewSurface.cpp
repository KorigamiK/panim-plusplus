#include "PreviewSurface.hpp"

#include <string>

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#endif

namespace panim::detail {

    Status create_preview_surface(WGPUInstance instance, SDL_Window *window, PreviewSurface &result) {
        if (!instance || !window)
            return Status::failure("Cannot create a surface without a window");

        WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;

#ifdef __APPLE__
        SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        auto *native_window = static_cast<NSWindow *>(SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr));
        if (!native_window) {
            return Status::failure("SDL did not expose its Cocoa window: " + std::string(SDL_GetError()));
        }
        NSView *content_view = native_window.contentView;
        content_view.wantsLayer = YES;
        CAMetalLayer *metal_layer = [CAMetalLayer layer];
        content_view.layer = metal_layer;
        WGPUSurfaceSourceMetalLayer source = WGPU_SURFACE_SOURCE_METAL_LAYER_INIT;
        source.layer = metal_layer;
        descriptor.nextInChain = &source.chain;
        result.surface = wgpuInstanceCreateSurface(instance, &descriptor);
#elif defined(_WIN32)
        SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        WGPUSurfaceSourceWindowsHWND source = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
        source.hinstance = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
        source.hwnd = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        descriptor.nextInChain = &source.chain;
        result.surface = wgpuInstanceCreateSurface(instance, &descriptor);
#elif defined(__ANDROID__)
        SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        WGPUSurfaceSourceAndroidNativeWindow source = WGPU_SURFACE_SOURCE_ANDROID_NATIVE_WINDOW_INIT;
        source.window = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
        descriptor.nextInChain = &source.chain;
        result.surface = wgpuInstanceCreateSurface(instance, &descriptor);
#elif defined(__linux__) || defined(__FreeBSD__)
        SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        void *wayland_display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        void *wayland_surface = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        if (wayland_display && wayland_surface) {
            WGPUSurfaceSourceWaylandSurface source = WGPU_SURFACE_SOURCE_WAYLAND_SURFACE_INIT;
            source.display = wayland_display;
            source.surface = wayland_surface;
            descriptor.nextInChain = &source.chain;
            result.surface = wgpuInstanceCreateSurface(instance, &descriptor);
        } else {
            WGPUSurfaceSourceXlibWindow source = WGPU_SURFACE_SOURCE_XLIB_WINDOW_INIT;
            source.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
            source.window = static_cast<uint64_t>(SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
            descriptor.nextInChain = &source.chain;
            result.surface = wgpuInstanceCreateSurface(instance, &descriptor);
        }
#else
        return Status::failure("Native WebGPU preview surfaces are unsupported on this platform");
#endif

        if (!result.surface) {
            destroy_preview_surface(result);
            return Status::failure("WebGPU could not wrap the SDL window");
        }
        return Status::success();
    }

    void destroy_preview_surface(PreviewSurface &surface) {
        if (surface.surface) {
            wgpuSurfaceRelease(surface.surface);
            surface.surface = nullptr;
        }
        surface.platform_view = nullptr;
    }

} // namespace panim::detail
