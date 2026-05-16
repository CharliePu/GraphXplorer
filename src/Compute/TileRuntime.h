#ifndef TILERUNTIME_H
#define TILERUNTIME_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ComputeBackend.h"
#include "TileJob.h"
#include "../Formula/FormulaCompiler.h"
#include "../Tile/TileCache.h"
#include "../Util/AsyncFrameInbox.h"
#include "../Util/Contracts.h"
#include "../Util/ThreadPool.h"

namespace gx
{
struct TileRuntimeDrainResult
{
    size_t applied{0};
    size_t rejected{0};
    std::vector<TileTransaction> transactions{};
};

class TileRuntime
{
public:
    explicit TileRuntime(std::unique_ptr<ComputeBackend> backend = std::make_unique<CpuComputeBackend>(),
                         size_t workerCount = 0);
    ~TileRuntime();

    void setLatestRequest(const ViewportRequest &request,
                          const CompiledFormula &formula);
    void submitJobs(std::span<const TileJob> jobs);
    [[nodiscard]] TileRuntimeDrainResult drainCompleted(
        TileCache &tileCache,
        std::unordered_map<uint64_t, RegionOutput> &regionPayloads,
        std::chrono::microseconds applyBudget = std::chrono::microseconds{2000});

    [[nodiscard]] size_t pendingCompletionCount() const;
    [[nodiscard]] size_t inFlightCount() const;

private:
    struct TileWorkResult
    {
        TileTransaction transaction{};
        std::vector<std::pair<uint64_t, RegionOutput>> regions{};
    };

    struct WorkKey
    {
        TileKey tile{};
        FormulaSemanticsHash semanticsHash{};
        JobKind kind{JobKind::ClassifyInterval};

        bool operator==(const WorkKey &) const = default;
    };

    struct WorkKeyHash
    {
        size_t operator()(const WorkKey &key) const noexcept;
    };

    void enqueueJob(const ViewportRequest &request, const CompiledFormula &formula, const TileJob &job);
    [[nodiscard]] TileWorkResult classifyTile(const ViewportRequest &request,
                                              const CompiledFormula &formula,
                                              const TileJob &job);
    [[nodiscard]] TileWorkResult rasterizeTile(const ViewportRequest &request,
                                               const CompiledFormula &formula,
                                               const TileJob &job);
    [[nodiscard]] bool isCurrent(const ViewportRequest &request) const;
    [[nodiscard]] bool isCurrent(const ContractHeader &header, FormulaSemanticsHash semanticsHash) const;
    void removeInFlight(const WorkKey &key);
    void discardInFlightExcept(FormulaSemanticsHash semanticsHash);
    [[nodiscard]] static size_t defaultWorkerCount();
    [[nodiscard]] static WorkKey workKeyFor(const ViewportRequest &request, const TileJob &job);

    std::unique_ptr<ComputeBackend> backend;
    mutable std::mutex backendMutex;
    mutable std::mutex stateMutex;
    std::optional<ViewportRequest> latestRequest{};
    std::optional<CompiledFormula> latestFormula{};
    std::atomic<uint64_t> latestSemanticsHash{0};
    std::atomic<uint64_t> nextRegionPayloadId{1};
    AsyncFrameInbox<TileWorkResult> completed{
        {AsyncFrameInbox<TileWorkResult>::Mode::HandleN, 8}
    };
    std::unordered_set<WorkKey, WorkKeyHash> inFlight;
    mutable std::mutex inFlightMutex;
    ThreadPool workers;
};
}

#endif // TILERUNTIME_H
