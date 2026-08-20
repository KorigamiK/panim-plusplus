#include "panim/Log.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace panim {

    void init_logging() {
        static bool initialized = false;
        if (initialized)
            return;
        auto logger = spdlog::stdout_color_mt("panim");
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%^%l%$] %v");
        spdlog::set_level(spdlog::level::info);
        initialized = true;
    }

} // namespace panim
