// Lightweight loader for animation plugins compiled as shared objects.
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "Animation.hpp"
#include "Status.hpp"

namespace panim {

    class PluginHost {
    public:
        explicit PluginHost(const std::filesystem::path &library_path);
        ~PluginHost();

        PluginHost(const PluginHost &) = delete;
        PluginHost &operator=(const PluginHost &) = delete;

        std::unique_ptr<Animation, std::function<void(Animation *)>> create();
        bool valid() const { return status_.ok; }
        const Status &status() const { return status_; }
        const std::filesystem::path &path() const { return path_; }

    private:
        void *handle_ = nullptr;
        CreateAnimationFn create_fn_ = nullptr;
        DestroyAnimationFn destroy_fn_ = nullptr;
        std::filesystem::path path_;
        Status status_ = Status::failure("uninitialized");
    };

} // namespace panim
