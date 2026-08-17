// Lightweight logging facade. Uses spdlog when available, otherwise falls back to stderr.
#pragma once

#ifdef PANIM_HAVE_SPDLOG
#include <spdlog/spdlog.h>
#define PANIM_LOG_TRACE(...) ::spdlog::trace(__VA_ARGS__)
#define PANIM_LOG_DEBUG(...) ::spdlog::debug(__VA_ARGS__)
#define PANIM_LOG_INFO(...) ::spdlog::info(__VA_ARGS__)
#define PANIM_LOG_WARN(...) ::spdlog::warn(__VA_ARGS__)
#define PANIM_LOG_ERROR(...) ::spdlog::error(__VA_ARGS__)
#else

#include <iostream>
#include <string_view>

namespace panim {
    inline void log_fallback(const char *level, const std::string_view msg) {
        std::cerr << "[" << level << "] " << msg << std::endl;
    }
} // namespace panim

#include <sstream>

namespace panim {
    template <typename... Args>
    inline std::string panim_format(std::string_view fmt, const Args &...args) {
        std::ostringstream oss;
        oss << fmt;
        ((oss << ' ' << args), ...);
        return oss.str();
    }
} // namespace panim

#define PANIM_LOG_TRACE(fmt, ...) \
    panim::log_fallback("TRACE", panim::panim_format(fmt __VA_OPT__(, ) __VA_ARGS__))
#define PANIM_LOG_DEBUG(fmt, ...) \
    panim::log_fallback("DEBUG", panim::panim_format(fmt __VA_OPT__(, ) __VA_ARGS__))
#define PANIM_LOG_INFO(fmt, ...) \
    panim::log_fallback("INFO", panim::panim_format(fmt __VA_OPT__(, ) __VA_ARGS__))
#define PANIM_LOG_WARN(fmt, ...) \
    panim::log_fallback("WARN", panim::panim_format(fmt __VA_OPT__(, ) __VA_ARGS__))
#define PANIM_LOG_ERROR(fmt, ...) \
    panim::log_fallback("ERROR", panim::panim_format(fmt __VA_OPT__(, ) __VA_ARGS__))

#endif

namespace panim {

    void init_logging();

} // namespace panim
