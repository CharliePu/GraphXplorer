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
    options.targetBatchLatency = std::max(options.targetBatchLatency, std::chrono::microseconds{1});
    options.maxIntervalBatchSize = std::max<size_t>(1, options.maxIntervalBatchSize);
    options.maxRasterBatchSize = std::max<size_t>(1, options.maxRasterBatchSize);
    options.initialIntervalBatchSize = std::max<size_t>(1, options.initialIntervalBatchSize);
    options.initialRasterBatchSize = std::max<size_t>(1, options.initialRasterBatchSize);
    options.initialIntervalBatchSize = std::min(options.initialIntervalBatchSize, options.maxIntervalBatchSize);
    options.initialRasterBatchSize = std::min(options.initialRasterBatchSize, options.maxRasterBatchSize);
    options.maxCandidatesPerKind = std::max<size_t>(1, options.maxCandidatesPerKind);
    options.explorationHeadroom = std::clamp(options.explorationHeadroom, 0.1, 1.0);
}

size_t BatchOptimizer::choose(const JobKind kind,
                              const size_t remainingJobs,
                              const uint32_t pixelsPerAxis) const
{
    if (remainingJobs == 0)
    {
        return 0;
    }

    std::lock_guard lock(mutex);
    const auto key = keyFor(kind, pixelsPerAxis);
    const auto limit = std::max<size_t>(1, std::min(remainingJobs, maxBatchSize(kind)));
    const auto frontierIt = frontierByKey.find(key);
    const auto observationsIt = observationsByKey.find(key);
    const auto empty = std::vector<BatchCandidate>{};
    const auto &frontier = frontierIt == frontierByKey.end() ? empty : frontierIt->second;
    const auto &observations = observationsIt == observationsByKey.end() ? empty : observationsIt->second;
    const BatchCandidate *best = nullptr;
    for (const auto &candidate : frontier)
    {
        if (candidate.latency > options.targetBatchLatency)
        {
            continue;
        }
        if (candidate.batchSize > limit)
        {
            continue;
        }
        if (!best
            || candidate.throughput() > best->throughput()
            || (std::abs(candidate.throughput() - best->throughput()) < 1e-12
                && candidate.batchSize > best->batchSize))
        {
            best = &candidate;
        }
    }

    if (const auto nextProbe = explorationCandidate(observations, best, limit))
    {
        return *nextProbe;
    }

    if (best)
    {
        return std::max<size_t>(1, best->batchSize);
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

uint64_t BatchOptimizer::keyFor(const JobKind kind, const uint32_t pixelsPerAxis)
{
    const auto kindPart = static_cast<uint64_t>(kind == JobKind::RasterizeRegion ? 1u : 0u);
    return (kindPart << 32u) | static_cast<uint64_t>(pixelsPerAxis);
}

std::optional<size_t> BatchOptimizer::explorationCandidate(
    const std::vector<BatchCandidate> &observations,
    const BatchCandidate *best,
    const size_t limit) const
{
    if (observations.empty())
    {
        return std::nullopt;
    }

    if (!best)
    {
        auto smallestTooSlow = size_t{0};
        for (const auto &observation : observations)
        {
            if (observation.latency <= options.targetBatchLatency || observation.batchSize <= 1)
            {
                continue;
            }
            if (smallestTooSlow == 0 || observation.batchSize < smallestTooSlow)
            {
                smallestTooSlow = observation.batchSize;
            }
        }
        if (smallestTooSlow > 1)
        {
            const auto probe = std::max<size_t>(1, std::min(limit, smallestTooSlow / 2));
            if (!hasObservation(observations, probe))
            {
                return probe;
            }
        }
        return std::nullopt;
    }

    const auto headroom = static_cast<int64_t>(
        std::ceil(static_cast<double>(options.targetBatchLatency.count()) * options.explorationHeadroom));
    const BatchCandidate *largestHeadroomFeasible = nullptr;
    for (const auto &observation : observations)
    {
        if (observation.batchSize > limit || observation.latency > options.targetBatchLatency)
        {
            continue;
        }
        if (observation.latency.count() <= headroom
            && (!largestHeadroomFeasible || observation.batchSize > largestHeadroomFeasible->batchSize))
        {
            largestHeadroomFeasible = &observation;
        }
    }

    if (largestHeadroomFeasible && largestHeadroomFeasible->batchSize < limit)
    {
        auto probe = largestHeadroomFeasible->batchSize > limit / 2
            ? limit
            : std::min(
                limit,
                std::max(largestHeadroomFeasible->batchSize + 1, largestHeadroomFeasible->batchSize * 2));
        if (probe > largestHeadroomFeasible->batchSize && !hasObservation(observations, probe))
        {
            return probe;
        }
    }

    auto nearestTooSlowAbove = size_t{0};
    if (largestHeadroomFeasible)
    {
        for (const auto &observation : observations)
        {
            if (observation.batchSize <= largestHeadroomFeasible->batchSize
                || observation.latency <= options.targetBatchLatency
                || observation.batchSize > limit)
            {
                continue;
            }
            if (nearestTooSlowAbove == 0 || observation.batchSize < nearestTooSlowAbove)
            {
                nearestTooSlowAbove = observation.batchSize;
            }
        }
    }
    if (largestHeadroomFeasible && nearestTooSlowAbove > largestHeadroomFeasible->batchSize + 1)
    {
        const auto probe = (largestHeadroomFeasible->batchSize + nearestTooSlowAbove) / 2;
        if (probe > largestHeadroomFeasible->batchSize && !hasObservation(observations, probe))
        {
            return probe;
        }
    }

    return std::nullopt;
}

bool BatchOptimizer::hasObservation(const std::vector<BatchCandidate> &observations, const size_t batchSize)
{
    return std::ranges::any_of(observations, [batchSize](const BatchCandidate &candidate)
    {
        return candidate.batchSize == batchSize;
    });
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
            const auto latencyNoWorse = other.latency <= candidate.latency;
            const auto throughputNoWorse = other.throughput() >= candidate.throughput();
            const auto strictlyBetter = other.latency < candidate.latency
                || other.throughput() > candidate.throughput();
            if (latencyNoWorse && throughputNoWorse && strictlyBetter)
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
        if (lhs.latency != rhs.latency)
        {
            return lhs.latency < rhs.latency;
        }
        return lhs.batchSize > rhs.batchSize;
    });

    if (frontier.size() > maxCandidates)
    {
        frontier.resize(maxCandidates);
    }
    candidates = std::move(frontier);
}
}
