#pragma once

#include <cstdio>
#include <format>
#include <utility>

namespace gpusim_log {

    template <class... Args>
    inline void write(const char* level, std::format_string<Args...> fmt, Args&&... args)
    {
        std::fprintf(stderr, "[gpusim][%s] %s\n", level,
                     std::format(fmt, std::forward<Args>(args)...).c_str());
    }

} // namespace gpusim_log

#define LOG_TRACE(...)    ::gpusim_log::write("trace", __VA_ARGS__)
#define LOG_DEBUG(...)    ::gpusim_log::write("debug", __VA_ARGS__)
#define LOG_INFO(...)     ::gpusim_log::write("info",  __VA_ARGS__)
#define LOG_WARN(...)     ::gpusim_log::write("warn",  __VA_ARGS__)
#define LOG_ERROR(...)    ::gpusim_log::write("error", __VA_ARGS__)
#define LOG_CRITICAL(...) ::gpusim_log::write("crit",  __VA_ARGS__)
