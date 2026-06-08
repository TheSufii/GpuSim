#pragma once
//
//  gpusim/log.h — minimal, dependency-free logging for the standalone library.
//
//  Replaces the engine's spdlog wrapper (logging/log.h), which was gpusim's only
//  coupling to DawnEngine. gpusim makes no log calls today; this header keeps the
//  LOG_* names available and writes to stderr so a standalone host still sees any
//  future warnings/errors. Formatting is std::format "{}" style, matching the
//  engine's old macros, so any LOG_ call site copies over unchanged.
//
//  Internal to the library: included only from .cpp, never from a public header,
//  so it can never clash with a host engine's own LOG_* macros. If you later want
//  library logs routed into the host's logger (spdlog, etc.), swap the body of
//  write() for a settable callback — a few lines, deliberately omitted for now.
//
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
