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
    size_t initialIntervalBatchSize{64};
    size_t initialRasterBatchSize{8};
    size_t maxIntervalBatchSize{1024};
    size_t maxRasterBatchSize{64};
    size_t maxCandidatesPerKind{64};
    double explorationGrowthFactor{2.0};
};

struct BatchCandidate
{
    size_t batchSize{1};
    std::chrono::microseconds latency{0};

    [[nodiscard]] double throughput() const;
    bool operator==(const BatchCandidate &) const = default;
};

class BackendBatchPolicy
{
public:
    virtual ~BackendBatchPolicy() = default;

    [[nodiscard]] virtual size_t choose(JobKind kind,
                                        size_t remainingJobs,
                                        uint32_t pixelsPerAxis = 0) = 0;
    virtual void observe(JobKind kind,
                         size_t batchSize,
                         std::chrono::microseconds latency,
                         uint32_t pixelsPerAxis = 0) = 0;
};

class BatchOptimizer final : public BackendBatchPolicy
{
public:
    explicit BatchOptimizer(BatchOptimizerOptions options = {});

    [[nodiscard]] size_t choose(JobKind kind, size_t remainingJobs, uint32_t pixelsPerAxis = 0) override;
    void observe(JobKind kind,
                 size_t batchSize,
                 std::chrono::microseconds latency,
                 uint32_t pixelsPerAxis = 0) override;
    [[nodiscard]] std::vector<BatchCandidate> frontier(JobKind kind, uint32_t pixelsPerAxis = 0) const;

private:
    [[nodiscard]] size_t fallbackBatchSize(JobKind kind) const;
    [[nodiscard]] size_t maxBatchSize(JobKind kind) const;
    [[nodiscard]] static uint64_t keyFor(JobKind kind, uint32_t pixelsPerAxis);
    [[nodiscard]] std::optional<size_t> explorationCandidate(const BatchCandidate *best,
                                                             size_t largestObserved,
                                                             size_t limit) const;
    static void pruneFrontier(std::vector<BatchCandidate> &candidates, size_t maxCandidates);

    BatchOptimizerOptions options{};
    mutable std::mutex mutex;
    std::unordered_map<uint64_t, std::vector<BatchCandidate>> observationsByKey;
    std::unordered_map<uint64_t, std::vector<BatchCandidate>> frontierByKey;
};
}

#endif // BATCHOPTIMIZER_H
