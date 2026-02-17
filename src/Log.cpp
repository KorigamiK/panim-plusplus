#include "panim/Log.hpp"

#ifdef PANIM_HAVE_SPDLOG
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

namespace panim {

    void init_logging() {
#ifdef PANIM_HAVE_SPDLOG
        static bool initialized = false;
        if (initialized)
            return;
        auto logger = spdlog::stdout_color_mt("panim");
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%^%l%$] %v");
        spdlog::set_level(spdlog::level::info);
        initialized = true;
#endif
    }

} // namespace panim
