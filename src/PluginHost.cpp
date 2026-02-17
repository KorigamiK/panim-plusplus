// Runtime loader for animation plugins.
#include "panim/PluginHost.hpp"
#include "panim/Log.hpp"

#include <dlfcn.h>
#include <utility>

namespace panim {

    namespace {

        template <typename Fn>
        Fn load_symbol(void *handle, const char *name) {
            dlerror();
            auto *sym = dlsym(handle, name);
            if (const char *err = dlerror()) {
                PANIM_LOG_ERROR("Failed to load symbol {}: {}", name, err);
                return nullptr;
            }
            return reinterpret_cast<Fn>(sym);
        }

    } // namespace

    PluginHost::PluginHost(const std::filesystem::path &library_path) : path_(library_path) {
        handle_ = dlopen(path_.c_str(), RTLD_LAZY | RTLD_LOCAL);
        if (!handle_) {
            status_ = Status::failure(std::string("Unable to open plugin: ") + dlerror());
            PANIM_LOG_ERROR(status_.message);
            return;
        }

        create_fn_ = load_symbol<CreateAnimationFn>(handle_, "create_animation");
        // destroy_animation is optional; fall back to delete.
        dlerror();
        destroy_fn_ = reinterpret_cast<DestroyAnimationFn>(dlsym(handle_, "destroy_animation"));
        dlerror();

        if (!create_fn_) {
            status_ = Status::failure("create_animation symbol missing");
            PANIM_LOG_ERROR(status_.message);
            return;
        }

        status_ = Status::success();
    }

    PluginHost::~PluginHost() {
        if (handle_) {
            dlclose(handle_);
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
