#ifndef PIPELINELOG_H
#define PIPELINELOG_H

#include <cstdio>
#include <chrono>
#include <cstdarg>

namespace PipelineLog
{
    inline FILE *logFile = nullptr;

    inline void init(const char *path = "pipeline.log")
    {
        logFile = fopen(path, "w");
    }

    inline void shutdown()
    {
        if (logFile)
        {
            fclose(logFile);
            logFile = nullptr;
        }
    }

    inline void log(const char *fmt, ...)
    {
        if (!logFile) return;

        const auto now = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();

        fprintf(logFile, "[%lld] ", static_cast<long long>(ms));

        va_list args;
        va_start(args, fmt);
        vfprintf(logFile, fmt, args);
        va_end(args);

        fprintf(logFile, "\n");
        fflush(logFile);
    }
}

#endif // PIPELINELOG_H
