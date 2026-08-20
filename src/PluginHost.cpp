// Runtime loader for animation plugins.
#include "panim/PluginHost.hpp"
#include "panim/Log.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <utility>

namespace panim {

    namespace {

        template <typename Fn> Fn load_symbol(void *handle, const char *name) {
#ifdef _WIN32
            auto symbol = GetProcAddress(static_cast<HMODULE>(handle), name);
            if (!symbol) {
                PANIM_LOG_ERROR("Failed to load symbol {} (Windows error {})", name, GetLastError());
                return nullptr;
            }
            return reinterpret_cast<Fn>(symbol);
#else
            dlerror();
            auto *sym = dlsym(handle, name);
            if (const char *err = dlerror()) {
                PANIM_LOG_ERROR("Failed to load symbol {}: {}", name, err);
                return nullptr;
            }
            return reinterpret_cast<Fn>(sym);
#endif
        }

    } // namespace

    PluginHost::PluginHost(const std::filesystem::path &library_path) : path_(library_path) {
#ifdef _WIN32
        handle_ = LoadLibraryW(path_.wstring().c_str());
        if (!handle_) {
            status_ = Status::failure("Unable to open plugin (Windows error " + std::to_string(GetLastError()) + ")");
            PANIM_LOG_ERROR(status_.message);
            return;
        }
#else
        handle_ = dlopen(path_.c_str(), RTLD_LAZY | RTLD_LOCAL);
        if (!handle_) {
            status_ = Status::failure(std::string("Unable to open plugin: ") + dlerror());
            PANIM_LOG_ERROR(status_.message);
            return;
        }
#endif

        api_version_fn_ = load_symbol<PluginApiVersionFn>(handle_, "panim_plugin_api_version");
        if (!api_version_fn_) {
            status_ = Status::failure("Plugin API version symbol missing; rebuild the plugin with "
                                      "PANIM_EXPORT_ANIMATION");
            PANIM_LOG_ERROR(status_.message);
            return;
        }
        uint32_t api_version = api_version_fn_();
        if (api_version != plugin_api_version) {
            status_ = Status::failure("Plugin API version mismatch: plugin=" + std::to_string(api_version) +
                                      ", engine=" + std::to_string(plugin_api_version));
            PANIM_LOG_ERROR(status_.message);
            return;
        }

        create_fn_ = load_symbol<CreateAnimationFn>(handle_, "create_animation");
        // destroy_animation is optional; fall back to delete.
#ifdef _WIN32
        destroy_fn_ = reinterpret_cast<DestroyAnimationFn>(GetProcAddress(static_cast<HMODULE>(handle_), "destroy_animation"));
#else
        dlerror();
        destroy_fn_ = reinterpret_cast<DestroyAnimationFn>(dlsym(handle_, "destroy_animation"));
        dlerror();
#endif

        if (!create_fn_) {
            status_ = Status::failure("create_animation symbol missing");
            PANIM_LOG_ERROR(status_.message);
            return;
        }

        status_ = Status::success();
    }

    PluginHost::~PluginHost() {
        if (handle_) {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(handle_));
#else
            dlclose(handle_);
#endif
        }
    }

    std::unique_ptr<Animation, std::function<void(Animation *)>> PluginHost::create() {
        if (!status_.ok || !create_fn_) {
            return {};
        }

        Animation *raw = create_fn_();
        auto deleter = [this](Animation *a) {
            if (!a)
                return;
            if (destroy_fn_) {
                destroy_fn_(a);
            } else {
                delete a;
            }
        };
        return std::unique_ptr<Animation, std::function<void(Animation *)>>(raw, deleter);
    }

} // namespace panim
