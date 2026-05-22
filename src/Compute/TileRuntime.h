#ifndef TILERUNTIME_H
#define TILERUNTIME_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "BatchOptimizer.h"
#include "ComputeBackend.h"
#include "TileJob.h"
#include "../Formula/FormulaCompiler.h"
#include "../Tile/TileCache.h"
#include "../Tile/TileMath.h"
#include "../Util/AsyncFrameInbox.h"
#include "../Util/Contracts.h"
#include "../Util/ThreadPool.h"

namespace gx
{
struct TileRuntimeDrainResult
{
    size_t applied{0};
    size_t rejected{0};
    size_t proofTreesApplied{0};
    size_t proofTreesRejected{0};
    std::vector<TileTransaction> transactions{};
};

struct TileRuntimeOptions
{
    BatchOptimizerOptions batchOptimizer{};
    size_t cpuWorkerHeadroom{2};
    size_t maxWorkerCount{0};
    uint32_t rasterPixelsPerAxis{RasterTexturePixels};
};

class TileRuntime
{
public:
    using CompletionCallback = std::function<void()>;

    explicit TileRuntime(std::unique_ptr<ComputeBackend> backend = nullptr,
                         size_t workerCount = 0,
                         TileRuntimeOptions options = {},
                         std::unique_ptr<BackendBatchPolicy> batchPolicy = nullptr);
    ~TileRuntime();

    void setLatestRequest(const ViewportRequest &request,
                          const CompiledFormula &formula);
    void setGpuPreviewAllowed(bool allowed);
    void setCompletionCallback(CompletionCallback callback);
    void submitJobs(std::span<const TileJob> jobs);
    [[nodiscard]] TileRuntimeDrainResult drainCompleted(
        TileCache &tileCache,
        std::unordered_map<uint64_t, RegionOutput> &regionPayloads,
        std::chrono::microseconds applyBudget = std::chrono::microseconds{2000});

    [[nodiscard]] size_t pendingCompletionCount() const;
    [[nodiscard]] size_t inFlightCount() const;
    [[nodiscard]] ThreadPool &workerPool();
    [[nodiscard]] static size_t recommendedWorkerCount(size_t hardwareThreads,
                                                       size_t headroom,
                                                       size_t maxWorkers = 0);

private:
    struct TileWorkResult
    {
        std::vector<TileTransaction> transactions{};
        std::vector<std::pair<uint64_t, RegionOutput>> regions{};
        std::vector<TileProofTreePatch> proofTrees{};
    };

    struct WorkKey
    {
        TileKey tile{};
        FormulaSemanticsHash semanticsHash{};
        JobKind kind{JobKind::ClassifyInterval};
        TexturePreparationMode textureMode{TexturePreparationMode::GpuPreview};

        bool operator==(const WorkKey &) const = default;
    };

    struct WorkKeyHash
    {
        size_t operator()(const WorkKey &key) const noexcept;
    };

    void enqueueBatch(const ViewportRequest &request,
                      const CompiledFormula &formula,
                      JobKind kind,
                      std::vector<TileJob> jobs,
                      uint32_t rasterPixelsPerAxis = 0,
                      TexturePreparationMode textureMode = TexturePreparationMode::GpuPreview);
    void enqueueBatches(const ViewportRequest &request,
                        const CompiledFormula &formula,
                        JobKind kind,
                        std::vector<TileJob> jobs,
                        uint32_t rasterPixelsPerAxis = 0,
                        TexturePreparationMode textureMode = TexturePreparationMode::GpuPreview);
    [[nodiscard]] TileWorkResult classifyTiles(const ViewportRequest &request,
                                               const CompiledFormula &formula,
                                               std::span<const TileJob> jobs);
    [[nodiscard]] TileWorkResult rasterizeTiles(const ViewportRequest &request,
                                                const CompiledFormula &formula,
                                                std::span<const TileJob> jobs,
                                                uint32_t pixelsPerAxis,
                                                TexturePreparationMode textureMode);
    [[nodiscard]] static TileWorkResult recoveryWork(const ViewportRequest &request,
                                                     JobKind kind,
                                                     std::span<const TileJob> jobs);
    static void appendRecoveryTransactions(TileWorkResult &work,
                                           const ViewportRequest &request,
                                           JobKind kind,
                                           std::span<const TileJob> jobs);
    [[nodiscard]] bool isCurrent(const ViewportRequest &request) const;
    [[nodiscard]] bool isCurrent(const ContractHeader &header, FormulaSemanticsHash semanticsHash) const;
    void removeInFlight(const WorkKey &key);
    void removeInFlight(const ViewportRequest &request, std::span<const TileJob> jobs);
    void discardInFlightExcept(FormulaSemanticsHash semanticsHash);
    void notifyCompletedWork();
    [[nodiscard]] size_t defaultWorkerCount() const;
    [[nodiscard]] static WorkKey workKeyFor(const ViewportRequest &request, const TileJob &job);
    [[nodiscard]] uint32_t rasterPixelsPerAxisFor(const ViewportRequest &request,
                                                  const TileKey &key) const;

    std::unique_ptr<ComputeBackend> backend;
    TileRuntimeOptions options{};
    std::unique_ptr<BackendBatchPolicy> batchPolicy;
    mutable std::mutex backendMutex;
    mutable std::mutex stateMutex;
    std::optional<ViewportRequest> latestRequest{};
    std::optional<CompiledFormula> latestFormula{};
    std::atomic<uint64_t> latestSemanticsHash{0};
    std::atomic<uint64_t> latestRequestId{0};
    std::atomic<uint64_t> nextRegionPayloadId{1};
    std::atomic<bool> gpuPreviewAllowed{true};
    AsyncFrameInbox<TileWorkResult> completed{
        {AsyncFrameInbox<TileWorkResult>::Mode::HandleN, 8}
    };
    std::unordered_set<WorkKey, WorkKeyHash> inFlight;
    mutable std::mutex inFlightMutex;
    CompletionCallback completionCallback{};
    mutable std::mutex completionCallbackMutex;
    ThreadPool workers;
};
}

#endif // TILERUNTIME_H
