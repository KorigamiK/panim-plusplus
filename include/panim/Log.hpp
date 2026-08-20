// Lightweight logging facade backed by spdlog.
#pragma once

#include <spdlog/spdlog.h>

#define PANIM_LOG_TRACE(...) ::spdlog::trace(__VA_ARGS__)
#define PANIM_LOG_DEBUG(...) ::spdlog::debug(__VA_ARGS__)
#define PANIM_LOG_INFO(...) ::spdlog::info(__VA_ARGS__)
#define PANIM_LOG_WARN(...) ::spdlog::warn(__VA_ARGS__)
#define PANIM_LOG_ERROR(...) ::spdlog::error(__VA_ARGS__)

namespace panim {

    void init_logging();

} // namespace panim
