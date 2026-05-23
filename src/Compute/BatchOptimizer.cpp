#include "BatchOptimizer.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace gx
{
double BatchCandidate::throughput() const
{
    const auto latencyUs = std::max<int64_t>(1, latency.count());
    return static_cast<double>(batchSize) / static_cast<double>(latencyUs);
}

BatchOptimizer::BatchOptimizer(BatchOptimizerOptions nextOptions): options{nextOptions}
{
    options.maxIntervalBatchSize = std::max<size_t>(1, options.maxIntervalBatchSize);
    options.maxRasterBatchSize = std::max<size_t>(1, options.maxRasterBatchSize);
    options.initialIntervalBatchSize = std::max<size_t>(1, options.initialIntervalBatchSize);
    options.initialRasterBatchSize = std::max<size_t>(1, options.initialRasterBatchSize);
    options.initialIntervalBatchSize = std::min(options.initialIntervalBatchSize, options.maxIntervalBatchSize);
    options.initialRasterBatchSize = std::min(options.initialRasterBatchSize, options.maxRasterBatchSize);
    options.maxCandidatesPerKind = std::max<size_t>(1, options.maxCandidatesPerKind);
    options.explorationGrowthFactor = std::clamp(options.explorationGrowthFactor, 1.1, 8.0);
    options.maxRasterBatchLatency = std::max(std::chrono::microseconds{0}, options.maxRasterBatchLatency);
}

size_t BatchOptimizer::choose(const JobKind kind,
                              const size_t remainingJobs,
                              const uint32_t pixelsPerAxis)
{
    if (remainingJobs == 0)
    {
        return 0;
    }

    std::lock_guard lock(mutex);
    const auto key = keyFor(kind, pixelsPerAxis);
    const auto limit = std::max<size_t>(1, std::min(remainingJobs, maxBatchSize(kind)));
    const auto latencyBudget = maxBatchLatency(kind);
    const auto frontierIt = frontierByKey.find(key);
    const auto empty = std::vector<BatchCandidate>{};
    const auto &frontier = frontierIt == frontierByKey.end() ? empty : frontierIt->second;
    const BatchCandidate *best = nullptr;
    auto largestObserved = size_t{0};
    for (const auto &candidate : frontier)
    {
        if (candidate.batchSize > limit)
        {
            continue;
        }
        if (latencyBudget > std::chrono::microseconds{0}
            && candidate.batchSize > 1
            && candidate.latency > latencyBudget)
        {
            continue;
        }
        largestObserved = std::max(largestObserved, candidate.batchSize);
        if (!best
            || candidate.throughput() > best->throughput()
            || (std::abs(candidate.throughput() - best->throughput()) < 1e-12
                && candidate.batchSize > best->batchSize))
        {
            best = &candidate;
        }
    }

    if (const auto nextProbe = explorationCandidate(best, largestObserved, limit))
    {
        if (shouldExploreCandidate(kind, *best, *nextProbe))
        {
            return *nextProbe;
        }
    }

    if (best)
    {
        return std::max<size_t>(1, best->batchSize);
    }

    if (!frontier.empty())
    {
        return latencyBudget > std::chrono::microseconds{0}
            ? std::min(limit, fallbackBatchSize(kind))
            : limit;
    }

    return std::min(limit, fallbackBatchSize(kind));
}

void BatchOptimizer::observe(const JobKind kind,
                             const size_t batchSize,
                             const std::chrono::microseconds latency,
                             const uint32_t pixelsPerAxis)
{
    if (batchSize == 0 || latency.count() <= 0)
    {
        return;
    }

    std::lock_guard lock(mutex);
    const auto key = keyFor(kind, pixelsPerAxis);
    auto &observations = observationsByKey[key];
    const BatchCandidate candidate{
        .batchSize = batchSize,
        .latency = latency
    };
    auto existing = std::ranges::find_if(observations, [batchSize](const BatchCandidate &entry)
    {
        return entry.batchSize == batchSize;
    });
    if (existing == observations.end())
    {
        observations.push_back(candidate);
    }
    else if (latency < existing->latency)
    {
        existing->latency = latency;
    }

    auto frontier = observations;
    pruneFrontier(frontier, options.maxCandidatesPerKind);
    frontierByKey[key] = std::move(frontier);
}

std::vector<BatchCandidate> BatchOptimizer::frontier(const JobKind kind, const uint32_t pixelsPerAxis) const
{
    std::lock_guard lock(mutex);
    const auto it = frontierByKey.find(keyFor(kind, pixelsPerAxis));
    return it == frontierByKey.end() ? std::vector<BatchCandidate>{} : it->second;
}

size_t BatchOptimizer::fallbackBatchSize(const JobKind kind) const
{
    return kind == JobKind::RasterizeRegion
        ? options.initialRasterBatchSize
        : options.initialIntervalBatchSize;
}

size_t BatchOptimizer::maxBatchSize(const JobKind kind) const
{
    return kind == JobKind::RasterizeRegion
        ? options.maxRasterBatchSize
        : options.maxIntervalBatchSize;
}

std::chrono::microseconds BatchOptimizer::maxBatchLatency(const JobKind kind) const
{
    return kind == JobKind::RasterizeRegion
        ? options.maxRasterBatchLatency
        : std::chrono::microseconds{0};
}

uint64_t BatchOptimizer::keyFor(const JobKind kind, const uint32_t pixelsPerAxis)
{
    const auto kindPart = static_cast<uint64_t>(kind == JobKind::RasterizeRegion ? 1u : 0u);
    return (kindPart << 32u) | static_cast<uint64_t>(pixelsPerAxis);
}

std::optional<size_t> BatchOptimizer::explorationCandidate(const BatchCandidate *best,
                                                           const size_t largestObserved,
                                                           const size_t limit) const
{
    if (!best || best->batchSize != largestObserved || largestObserved >= limit)
    {
        return std::nullopt;
    }

    const auto grown = static_cast<size_t>(
        std::ceil(static_cast<double>(largestObserved) * options.explorationGrowthFactor));
    return std::min(limit, std::max(largestObserved + 1, grown));
}

bool BatchOptimizer::shouldExploreCandidate(const JobKind kind,
                                            const BatchCandidate &best,
                                            const size_t candidateBatchSize) const
{
    const auto latencyBudget = maxBatchLatency(kind);
    if (latencyBudget <= std::chrono::microseconds{0}
        || candidateBatchSize <= best.batchSize
        || best.batchSize == 0
        || best.latency <= std::chrono::microseconds{0})
    {
        return true;
    }

    const auto estimatedLatency = std::chrono::microseconds{
        (best.latency.count() * static_cast<int64_t>(candidateBatchSize))
        / static_cast<int64_t>(best.batchSize)
    };
    return estimatedLatency <= latencyBudget;
}

void BatchOptimizer::pruneFrontier(std::vector<BatchCandidate> &candidates, const size_t maxCandidates)
{
    std::ranges::sort(candidates, [](const BatchCandidate &lhs, const BatchCandidate &rhs)
    {
        if (lhs.batchSize != rhs.batchSize)
        {
            return lhs.batchSize < rhs.batchSize;
        }
        return lhs.latency < rhs.latency;
    });

    candidates.erase(
        std::unique(candidates.begin(), candidates.end(), [](const BatchCandidate &lhs,
                                                             const BatchCandidate &rhs)
        {
            return lhs.batchSize == rhs.batchSize;
        }),
        candidates.end());

    std::vector<BatchCandidate> frontier;
    frontier.reserve(candidates.size());
    for (const auto &candidate : candidates)
    {
        auto dominated = false;
        for (const auto &other : candidates)
        {
            if (&candidate == &other)
            {
                continue;
            }
            const auto throughputNoWorse = other.throughput() >= candidate.throughput();
            const auto batchSizeNoWorse = other.batchSize >= candidate.batchSize;
            const auto strictlyBetter = other.throughput() > candidate.throughput()
                || other.batchSize > candidate.batchSize;
            if (throughputNoWorse && batchSizeNoWorse && strictlyBetter)
            {
                dominated = true;
                break;
            }
        }
        if (!dominated)
        {
            frontier.push_back(candidate);
        }
    }

    std::ranges::sort(frontier, [](const BatchCandidate &lhs, const BatchCandidate &rhs)
    {
        if (std::abs(lhs.throughput() - rhs.throughput()) >= 1e-12)
        {
            return lhs.throughput() > rhs.throughput();
        }
        if (lhs.batchSize != rhs.batchSize)
        {
            return lhs.batchSize > rhs.batchSize;
        }
        return lhs.latency < rhs.latency;
    });

    if (frontier.size() > maxCandidates)
    {
        frontier.resize(maxCandidates);
    }
    candidates = std::move(frontier);
}
}
