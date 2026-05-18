#ifndef PIPELINELOG_H
#define PIPELINELOG_H

#include <cstdio>
#include <chrono>
#include <cstdarg>
#include <mutex>

namespace PipelineLog
{
    using Clock = std::chrono::steady_clock;

    inline FILE *logFile = nullptr;
    inline std::mutex logMutex;
    inline Clock::time_point startTime{};
    inline Clock::time_point previousLogTime{};

    inline void init(const char *path = "pipeline.log")
    {
        std::lock_guard lock{logMutex};
        logFile = fopen(path, "w");
        startTime = Clock::now();
        previousLogTime = startTime;
    }

    inline void shutdown()
    {
        std::lock_guard lock{logMutex};
        if (logFile)
        {
            fclose(logFile);
            logFile = nullptr;
        }
    }

    inline void vlog(const bool mirrorToConsole, const char *fmt, va_list args)
    {
        std::lock_guard lock{logMutex};
        if (!logFile) return;

        const auto now = Clock::now();
        const auto sinceStartUs = std::chrono::duration_cast<std::chrono::microseconds>(
            now - startTime).count();
        const auto sincePreviousUs = std::chrono::duration_cast<std::chrono::microseconds>(
            now - previousLogTime).count();
        previousLogTime = now;

        const auto writePrefix = [&](FILE *target)
        {
            fprintf(
                target,
                "[%lld us +%lld us] ",
                static_cast<long long>(sinceStartUs),
                static_cast<long long>(sincePreviousUs));
        };

        if (logFile)
        {
            va_list fileArgs;
            va_copy(fileArgs, args);
            writePrefix(logFile);
            vfprintf(logFile, fmt, fileArgs);
            va_end(fileArgs);

            fprintf(logFile, "\n");
            fflush(logFile);
        }

        if (mirrorToConsole)
        {
            va_list consoleArgs;
            va_copy(consoleArgs, args);
            writePrefix(stdout);
            vfprintf(stdout, fmt, consoleArgs);
            va_end(consoleArgs);

            fprintf(stdout, "\n");
            fflush(stdout);
        }
    }

    inline void log(const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        vlog(false, fmt, args);
        va_end(args);
    }

    inline void logConsole(const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        vlog(true, fmt, args);
        va_end(args);
    }
}

#endif // PIPELINELOG_H
