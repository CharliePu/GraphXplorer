// Lightweight opt-in runtime profiler for interactive hotspot analysis.
// Enable with env var: GRAPHX_PROFILE=1

#ifndef PERFORMANCEPROFILER_H
#define PERFORMANCEPROFILER_H

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace perf
{
struct ProfileStat
{
    uint64_t count{0};
    uint64_t totalNs{0};
    uint64_t maxNs{0};
};

inline bool enabled()
{
    static const bool on = []()
    {
        const auto *env = std::getenv("GRAPHX_PROFILE");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    return on;
}

inline std::mutex &mutex()
{
    static std::mutex m;
    return m;
}

inline std::unordered_map<std::string, ProfileStat> &stats()
{
    static std::unordered_map<std::string, ProfileStat> s;
    return s;
}

inline std::ofstream &output()
{
    static std::ofstream out("graphx_profile.log", std::ios::out | std::ios::trunc);
    return out;
}

inline std::chrono::steady_clock::time_point &startTime()
{
    static const auto start = std::chrono::steady_clock::now();
    static auto storage = start;
    return storage;
}

inline std::chrono::steady_clock::time_point &nextFlushTime()
{
    static auto next = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    return next;
}

inline void addSample(const char *name, const uint64_t elapsedNs)
{
    if (!enabled() || !name)
    {
        return;
    }

    std::lock_guard lock(mutex());
    auto &entry = stats()[name];
    entry.count += 1;
    entry.totalNs += elapsedNs;
    entry.maxNs = std::max(entry.maxNs, elapsedNs);
}

inline void flushNow()
{
    if (!enabled())
    {
        return;
    }

    std::vector<std::pair<std::string, ProfileStat>> snapshot;
    {
        std::lock_guard lock(mutex());
        if (stats().empty())
        {
            return;
        }

        snapshot.reserve(stats().size());
        for (const auto &[name, stat] : stats())
        {
            snapshot.emplace_back(name, stat);
        }
        stats().clear();
    }

    std::ranges::sort(snapshot, [](const auto &lhs, const auto &rhs)
    {
        return lhs.second.totalNs > rhs.second.totalNs;
    });

    auto &out = output();
    if (!out.is_open())
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime()).count();
    out << "=== Profile @" << elapsedMs << " ms ===\n";
    out << "name,count,total_ms,avg_ms,max_ms\n";
    for (const auto &[name, stat] : snapshot)
    {
        const auto totalMs = static_cast<double>(stat.totalNs) / 1'000'000.0;
        const auto avgMs = stat.count > 0 ? totalMs / static_cast<double>(stat.count) : 0.0;
        const auto maxMs = static_cast<double>(stat.maxNs) / 1'000'000.0;
        out << name << "," << stat.count << "," << totalMs << "," << avgMs << "," << maxMs << "\n";
    }
    out << "\n";
    out.flush();
}

inline void flushIfDue()
{
    if (!enabled())
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now < nextFlushTime())
    {
        return;
    }

    flushNow();
    nextFlushTime() = now + std::chrono::milliseconds(500);
}

class ScopedTimer
{
public:
    explicit ScopedTimer(const char *name): name{name}, start{std::chrono::steady_clock::now()}
    {
    }

    ~ScopedTimer()
    {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsedNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        addSample(name, elapsedNs);
    }

private:
    const char *name;
    std::chrono::steady_clock::time_point start;
};
} // namespace perf

#define GRAPHX_PROFILE_SCOPE(name) ::perf::ScopedTimer _graphxProfileTimer##__LINE__(name)
#define GRAPHX_PROFILE_FLUSH_IF_DUE() ::perf::flushIfDue()
#define GRAPHX_PROFILE_FLUSH_NOW() ::perf::flushNow()

#endif // PERFORMANCEPROFILER_H
