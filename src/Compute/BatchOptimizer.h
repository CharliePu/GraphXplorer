#ifndef BATCHOPTIMIZER_H
#define BATCHOPTIMIZER_H

#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "TileJob.h"

namespace gx
{
struct BatchOptimizerOptions
{
    std::chrono::microseconds targetBatchLatency{12000};
    size_t initialIntervalBatchSize{64};
    size_t initialRasterBatchSize{1};
    size_t maxIntervalBatchSize{1024};
    size_t maxRasterBatchSize{1};
    size_t maxCandidatesPerKind{64};
    double explorationHeadroom{0.75};
};

struct BatchCandidate
{
    size_t batchSize{1};
    std::chrono::microseconds latency{0};

    [[nodiscard]] double throughput() const;
    bool operator==(const BatchCandidate &) const = default;
};

class BatchOptimizer
{
public:
    explicit BatchOptimizer(BatchOptimizerOptions options = {});

    [[nodiscard]] size_t choose(JobKind kind, size_t remainingJobs, uint32_t pixelsPerAxis = 0) const;
    void observe(JobKind kind,
                 size_t batchSize,
                 std::chrono::microseconds latency,
                 uint32_t pixelsPerAxis = 0);
    [[nodiscard]] std::vector<BatchCandidate> frontier(JobKind kind, uint32_t pixelsPerAxis = 0) const;

private:
    [[nodiscard]] size_t fallbackBatchSize(JobKind kind) const;
    [[nodiscard]] size_t maxBatchSize(JobKind kind) const;
    [[nodiscard]] static uint64_t keyFor(JobKind kind, uint32_t pixelsPerAxis);
    [[nodiscard]] std::optional<size_t> explorationCandidate(
        const std::vector<BatchCandidate> &observations,
        const BatchCandidate *best,
        size_t limit) const;
    [[nodiscard]] static bool hasObservation(const std::vector<BatchCandidate> &observations,
                                             size_t batchSize);
    static void pruneFrontier(std::vector<BatchCandidate> &candidates, size_t maxCandidates);

    BatchOptimizerOptions options{};
    mutable std::mutex mutex;
    std::unordered_map<uint64_t, std::vector<BatchCandidate>> observationsByKey;
    std::unordered_map<uint64_t, std::vector<BatchCandidate>> frontierByKey;
};
}

#endif // BATCHOPTIMIZER_H
