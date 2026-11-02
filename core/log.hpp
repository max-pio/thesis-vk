// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

/// Importance of a log message. Messages below the level set in logInit() are skipped.
enum class LogLevel : uint8_t
{
    Debug,
    Info,
    Warn,
    Error
};

/// Names of the log levels as used on the command line (--log-level), in the order of LogLevel
inline constexpr const char* LOG_LEVEL_NAMES[] = {"debug", "info", "warn", "error"};

/// Minimum level of messages that are written, set in logInit()
extern LogLevel gLogLevel;

/// Call once at the start of main() before logging. Messages below level are skipped. With a logFile path, the log is
/// additionally written to that file (overwritten). Returns false if the file cannot be opened.
bool logInit(LogLevel level, const std::string& logFile);
/// Closes the log file
void logShutdown();

/// Use the LOG_* macros instead. Writes one line with printf-style formatting to the console (stderr) and the log
/// file. Thread-safe: lines written by different threads at the same time do not mix.
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void logWrite(LogLevel level, const char* format, ...);

/// Log macros with printf-style formatting, e.g. LOG_INFO("GPU: %s", name.c_str()) or LOG_WARN("%d errors", count).
/// The arguments are not evaluated if the level is below gLogLevel, so LOG_DEBUG costs nothing when it is disabled.
#define LOG(level, ...)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        if (gLogLevel <= (level))                                                                                      \
            logWrite((level), __VA_ARGS__);                                                                            \
    } while (0)
#define LOG_DEBUG(...) LOG(LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...) LOG(LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...) LOG(LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) LOG(LogLevel::Error, __VA_ARGS__)

// Temporary timing for debugging -------------------------------------------------------------------------------------

/// Time point of the program start for DLOG()
inline const std::chrono::steady_clock::time_point DLOG_START = std::chrono::steady_clock::now();
/// Time point of the last DLOG() of each thread
inline thread_local std::chrono::steady_clock::time_point gDlogLast = DLOG_START;

/// Returns the milliseconds since the program start and since the last DLOG() of the calling thread
inline void dlogTimes(double& sinceStartMs, double& sinceLastMs)
{
    const auto now = std::chrono::steady_clock::now();
    sinceStartMs = std::chrono::duration<double, std::milli>(now - DLOG_START).count();
    sinceLastMs = std::chrono::duration<double, std::milli>(now - gDlogLast).count();
    gDlogLast = now;
}

/// Quick timing while debugging, remove it before committing (permanent measurements: CpuTimer / GpuTimer).
/// DLOG("upload") or DLOG("upload %d MB", size) logs the time since the program start and since the last DLOG(), e.g.
/// "[INFO]  [D]  463.0 ms  +12.3 ms  upload 488 MB".
#define DLOG(format, ...)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        double dlogSinceStartMs, dlogSinceLastMs;                                                                      \
        dlogTimes(dlogSinceStartMs, dlogSinceLastMs);                                                                  \
        LOG_INFO("[D] %8.1f ms %+8.1f ms  " format, dlogSinceStartMs, dlogSinceLastMs __VA_OPT__(, ) __VA_ARGS__);     \
    } while (0)
