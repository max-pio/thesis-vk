// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#include "log.hpp"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

LogLevel gLogLevel = LogLevel::Info;

namespace {

std::FILE* gLogFile = nullptr;
bool gConsoleColors = false;
/// keeps lines of different threads apart, and in the same order on the console and in the file
std::mutex gLogMutex;

constexpr const char* LABELS[] = {"[DEBUG] ", "[INFO]  ", "[WARN]  ", "[ERROR] "};
// ANSI escape codes: green, default, yellow, red
constexpr const char* COLORS[] = {"\033[32m", "\033[0m", "\033[33m", "\033[31m"};
constexpr const char* COLOR_RESET = "\033[0m";

} // namespace

bool logInit(LogLevel level, const std::string& logFile)
{
    gLogLevel = level;
#ifndef _WIN32
    // colors only in a terminal, not if the output is redirected to a file
    gConsoleColors = isatty(fileno(stderr));
#endif
    if (!logFile.empty())
    {
        gLogFile = std::fopen(logFile.c_str(), "w");
        if (gLogFile == nullptr)
        {
            LOG_ERROR("Cannot open the log file %s", logFile.c_str());
            return false;
        }
    }
    return true;
}

void logShutdown()
{
    if (gLogFile != nullptr)
        std::fclose(gLogFile);
    gLogFile = nullptr;
}

void logWrite(LogLevel level, const char* format, ...)
{
    const int levelIndex = static_cast<int>(level);

    // Write label, message and newline into one buffer, so that each line is written with a single call. Most lines fit
    // into the buffer on the stack, longer ones are formatted again into a larger buffer.
    char stackBuffer[1024];
    char* buffer = stackBuffer;
    std::vector<char> heapBuffer;
    int length = std::snprintf(buffer, sizeof(stackBuffer), "%s", LABELS[levelIndex]);

    va_list args;
    va_start(args, format);
    va_list argsCopy;
    va_copy(argsCopy, args);
    const int messageLength = std::vsnprintf(buffer + length, sizeof(stackBuffer) - length, format, args);
    va_end(args);
    if (messageLength >= 0 && length + messageLength + 2 > static_cast<int>(sizeof(stackBuffer)))
    {
        heapBuffer.resize(length + messageLength + 2);
        buffer = heapBuffer.data();
        std::snprintf(buffer, heapBuffer.size(), "%s", LABELS[levelIndex]);
        std::vsnprintf(buffer + length, heapBuffer.size() - length, format, argsCopy);
    }
    va_end(argsCopy);

    if (messageLength > 0)
        length += messageLength;
    // messages of other libraries (e.g. the validation layers) may already end with a newline
    if (length == 0 || buffer[length - 1] != '\n')
        buffer[length++] = '\n';

    std::lock_guard lock(gLogMutex);
    if (gConsoleColors)
    {
        std::fputs(COLORS[levelIndex], stderr);
        std::fwrite(buffer, 1, length - 1, stderr);
        std::fputs(COLOR_RESET, stderr);
        std::fputc('\n', stderr);
    }
    else
    {
        std::fwrite(buffer, 1, length, stderr);
    }
    if (gLogFile != nullptr)
    {
        std::fwrite(buffer, 1, length, gLogFile);
        // errors are often followed by std::abort(), which does not write buffered output
        if (level >= LogLevel::Warn)
            std::fflush(gLogFile);
    }
}
