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

    inline void log(const char *fmt, ...)
    {
        std::lock_guard lock{logMutex};
        if (!logFile) return;

        const auto now = Clock::now();
        const auto sinceStartUs = std::chrono::duration_cast<std::chrono::microseconds>(
            now - startTime).count();
        const auto sincePreviousUs = std::chrono::duration_cast<std::chrono::microseconds>(
            now - previousLogTime).count();
        previousLogTime = now;

        fprintf(
            logFile,
            "[%lld us +%lld us] ",
            static_cast<long long>(sinceStartUs),
            static_cast<long long>(sincePreviousUs));

        va_list args;
        va_start(args, fmt);
        vfprintf(logFile, fmt, args);
        va_end(args);

        fprintf(logFile, "\n");
        fflush(logFile);
    }
}

#endif // PIPELINELOG_H
