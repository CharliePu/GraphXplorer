#include "TileRuntime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <thread>
#include <utility>

#include "../Tile/TileMath.h"
#include "../Util/PerformanceProfiler.h"

namespace gx
{
size_t TileRuntime::WorkKeyHash::operator()(const WorkKey &key) const noexcept
{
    auto hash = TileKeyHash{}(key.tile);
    hash ^= std::hash<uint64_t>{}(key.semanticsHash.value) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    hash ^= std::hash<int>{}(static_cast<int>(key.kind)) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

TileRuntime::TileRuntime(std::unique_ptr<ComputeBackend> nextBackend, const size_t workerCount)
    : backend{nextBackend ? std::move(nextBackend) : makeDefaultComputeBackend()},
      workers{workerCount == 0 ? defaultWorkerCount() : workerCount}
{
}

TileRuntime::~TileRuntime()
{
    latestSemanticsHash.store(std::numeric_limits<uint64_t>::max(), std::memory_order_release);
    workers.clearAllTasks();
}

void TileRuntime::setLatestRequest(const ViewportRequest &request,
                                   const CompiledFormula &formula)
{
    if (!request.valid() || !formula.diagnostics.ok)
    {
        return;
    }

    const auto previousSemantics = latestSemanticsHash.load(std::memory_order_acquire);
    if (previousSemantics != 0 && previousSemantics != request.formula.semanticsHash.value)
    {
        workers.clearAllTasks();
        discardInFlightExcept(request.formula.semanticsHash);
    }

    {
        std::lock_guard lock(stateMutex);
        latestRequest = request;
        latestFormula = formula;
        latestSemanticsHash.store(request.formula.semanticsHash.value, std::memory_order_release);
    }
}

void TileRuntime::submitJobs(const std::span<const TileJob> jobs)
{
    GRAPHX_PROFILE_SCOPE("runtime.submitJobs");
    std::optional<ViewportRequest> request;
    std::optional<CompiledFormula> formula;
    {
        std::lock_guard lock(stateMutex);
        request = latestRequest;
        formula = latestFormula;
    }

    if (!request || !formula || !request->valid() || !formula->diagnostics.ok)
    {
        return;
    }

    std::vector<TileJob> intervalJobs;
    std::vector<TileJob> rasterJobs;
    intervalJobs.reserve(jobs.size());
    rasterJobs.reserve(jobs.size());
    for (const auto &job : jobs)
    {
        if (job.kind != JobKind::ClassifyInterval && job.kind != JobKind::RasterizeRegion)
        {
            continue;
        }
        const auto key = workKeyFor(*request, job);
        {
            std::lock_guard lock(inFlightMutex);
            if (!inFlight.insert(key).second)
            {
                continue;
            }
        }

        if (job.kind == JobKind::ClassifyInterval)
        {
            intervalJobs.push_back(job);
        }
        else
        {
            rasterJobs.push_back(job);
        }
    }

    if (!intervalJobs.empty())
    {
        enqueueBatch(*request, *formula, JobKind::ClassifyInterval, std::move(intervalJobs));
    }
    if (!rasterJobs.empty())
    {
        enqueueBatch(*request, *formula, JobKind::RasterizeRegion, std::move(rasterJobs));
    }
}

TileRuntimeDrainResult TileRuntime::drainCompleted(TileCache &tileCache,
                                                   std::unordered_map<uint64_t, RegionOutput> &regionPayloads,
                                                   const std::chrono::microseconds applyBudget)
{
    GRAPHX_PROFILE_SCOPE("runtime.drainCompleted");
    (void)applyBudget;
    TileRuntimeDrainResult result;
    for (auto &work : completed.drainForFrame())
    {
        for (auto &region : work.regions)
        {
            regionPayloads[region.first] = std::move(region.second);
        }

        for (auto &transaction : work.transactions)
        {
            if (transaction.deltas.empty())
            {
                continue;
            }
            if (!isCurrent(transaction.header, transaction.semanticsHash))
            {
                result.rejected += transaction.deltas.size();
                continue;
            }

            TileApplyResult applyResult;
            {
                GRAPHX_PROFILE_SCOPE("runtime.applyTransaction");
                applyResult = tileCache.apply(transaction);
            }
            result.applied += applyResult.applied;
            result.rejected += applyResult.rejected;
            result.transactions.push_back(std::move(transaction));
        }
    }

    return result;
}

size_t TileRuntime::pendingCompletionCount() const
{
    return completed.pendingCount();
}

size_t TileRuntime::inFlightCount() const
{
    std::lock_guard lock(inFlightMutex);
    return inFlight.size();
}

void TileRuntime::enqueueBatch(const ViewportRequest &request,
                               const CompiledFormula &formula,
                               const JobKind kind,
                               std::vector<TileJob> jobs)
{
    workers.addTask([this, request, formula, kind, jobs = std::move(jobs)]()
    {
        GRAPHX_PROFILE_SCOPE("runtime.workerBatch");
        if (!isCurrent(request))
        {
            removeInFlight(request, jobs);
            return;
        }

        TileWorkResult work;
        if (kind == JobKind::ClassifyInterval)
        {
            work = classifyTiles(request, formula, jobs);
        }
        else if (kind == JobKind::RasterizeRegion)
        {
            work = rasterizeTiles(request, formula, jobs);
        }

        if (isCurrent(request) && !work.transactions.empty())
        {
            completed.push(std::move(work));
        }

        removeInFlight(request, jobs);
    });
}

TileRuntime::TileWorkResult TileRuntime::classifyTiles(const ViewportRequest &request,
                                                       const CompiledFormula &formula,
                                                       const std::span<const TileJob> jobs)
{
    GRAPHX_PROFILE_SCOPE("runtime.classifyTiles");
    TileWorkResult work;
    if (jobs.empty())
    {
        return work;
    }

    std::vector<TileKey> keys;
    std::vector<double> xMin;
    std::vector<double> xMax;
    std::vector<double> yMin;
    std::vector<double> yMax;
    keys.reserve(jobs.size());
    xMin.reserve(jobs.size());
    xMax.reserve(jobs.size());
    yMin.reserve(jobs.size());
    yMax.reserve(jobs.size());
    for (const auto &job : jobs)
    {
        const auto bounds = tileBounds(job.key);
        keys.push_back(job.key);
        xMin.push_back(bounds.xMin);
        xMax.push_back(bounds.xMax);
        yMin.push_back(bounds.yMin);
        yMax.push_back(bounds.yMax);
    }
    std::vector<TileClassificationResult> classifications(jobs.size());

    BatchResult batchResult;
    {
        std::lock_guard lock(backendMutex);
        GRAPHX_PROFILE_SCOPE("runtime.backend.classify");
        batchResult = backend->classifyIntervals(
            IntervalBatchView{
                &formula,
                keys,
                xMin,
                xMax,
                yMin,
                yMax,
                [this, request]() { return !isCurrent(request); }
            },
            classifications);
    }

    if (!batchResult.ok || !isCurrent(request))
    {
        return work;
    }

    const auto completedCount = std::min(batchResult.completed, classifications.size());
    work.transactions.reserve(completedCount);
    for (size_t index = 0; index < completedCount; ++index)
    {
        const auto &classification = classifications[index];
        auto finalStage = TileStage::MixedNeedsRegion;
        if (classification.classification == TileClassification::UniformTrue)
        {
            finalStage = TileStage::UniformTrue;
        }
        else if (classification.classification == TileClassification::UniformFalse)
        {
            finalStage = TileStage::UniformFalse;
        }

        TileTransaction transaction{
            .header = request.header,
            .semanticsHash = request.formula.semanticsHash
        };
        transaction.deltas.reserve(3);
        transaction.deltas.push_back({
            .header = request.header,
            .semanticsHash = request.formula.semanticsHash,
            .key = classification.key,
            .stage = TileStage::IntervalQueued,
            .classification = TileClassification::Unknown
        });
        transaction.deltas.push_back({
            .header = request.header,
            .semanticsHash = request.formula.semanticsHash,
            .key = classification.key,
            .stage = TileStage::IntervalReady,
            .classification = classification.classification,
            .interval = classification.interval
        });
        transaction.deltas.push_back({
            .header = request.header,
            .semanticsHash = request.formula.semanticsHash,
            .key = classification.key,
            .stage = finalStage,
            .classification = classification.classification,
            .interval = classification.interval
        });
        work.transactions.push_back(std::move(transaction));
    }
    return work;
}

TileRuntime::TileWorkResult TileRuntime::rasterizeTiles(const ViewportRequest &request,
                                                        const CompiledFormula &formula,
                                                        const std::span<const TileJob> jobs)
{
    GRAPHX_PROFILE_SCOPE("runtime.rasterizeTiles");
    TileWorkResult work;
    if (jobs.empty())
    {
        return work;
    }

    std::vector<TileKey> keys;
    std::vector<double> xMin;
    std::vector<double> xMax;
    std::vector<double> yMin;
    std::vector<double> yMax;
    std::vector<uint32_t> offsets;
    keys.reserve(jobs.size());
    xMin.reserve(jobs.size());
    xMax.reserve(jobs.size());
    yMin.reserve(jobs.size());
    yMax.reserve(jobs.size());
    offsets.reserve(jobs.size());
    const auto pixelsPerTile = RasterTexturePixels * RasterTexturePixels;
    for (const auto &job : jobs)
    {
        const auto bounds = tileBounds(job.key);
        keys.push_back(job.key);
        xMin.push_back(bounds.xMin);
        xMax.push_back(bounds.xMax);
        yMin.push_back(bounds.yMin);
        yMax.push_back(bounds.yMax);
        offsets.push_back(static_cast<uint32_t>(offsets.size() * pixelsPerTile));
    }
    std::vector<RegionOutput> outputs(jobs.size());

    BatchResult batchResult;
    {
        std::lock_guard lock(backendMutex);
        GRAPHX_PROFILE_SCOPE("runtime.backend.raster");
        batchResult = backend->rasterizeRegions(
            RasterBatchView{
                &formula,
                keys,
                xMin,
                xMax,
                yMin,
                yMax,
                offsets,
                RasterTexturePixels,
                [this, request]() { return !isCurrent(request); }
            },
            outputs);
    }

    if (!batchResult.ok || !isCurrent(request))
    {
        return work;
    }

    const auto completedCount = std::min(batchResult.completed, outputs.size());
    work.transactions.reserve(completedCount);
    work.regions.reserve(completedCount);
    for (size_t index = 0; index < completedCount; ++index)
    {
        const auto payloadId = nextRegionPayloadId.fetch_add(1, std::memory_order_relaxed);
        auto output = std::move(outputs[index]);
        const auto width = static_cast<int>(output.width);
        const auto height = static_cast<int>(output.height);

        TileTransaction transaction{
            .header = request.header,
            .semanticsHash = request.formula.semanticsHash
        };
        transaction.deltas.reserve(2);
        transaction.deltas.push_back({
            .header = request.header,
            .semanticsHash = request.formula.semanticsHash,
            .key = output.key,
            .stage = TileStage::RegionQueued,
            .classification = TileClassification::Mixed
        });
        transaction.deltas.push_back({
            .header = request.header,
            .semanticsHash = request.formula.semanticsHash,
            .key = output.key,
            .stage = TileStage::RegionReady,
            .classification = TileClassification::Mixed,
            .region = RegionImageRef{payloadId, width, height}
        });
        work.transactions.push_back(std::move(transaction));
        work.regions.emplace_back(payloadId, std::move(output));
    }
    return work;
}

bool TileRuntime::isCurrent(const ViewportRequest &request) const
{
    return isCurrent(request.header, request.formula.semanticsHash);
}

bool TileRuntime::isCurrent(const ContractHeader &header, const FormulaSemanticsHash semanticsHash) const
{
    (void)header;
    return latestSemanticsHash.load(std::memory_order_acquire) == semanticsHash.value;
}

void TileRuntime::removeInFlight(const WorkKey &key)
{
    std::lock_guard lock(inFlightMutex);
    inFlight.erase(key);
}

void TileRuntime::removeInFlight(const ViewportRequest &request, const std::span<const TileJob> jobs)
{
    std::lock_guard lock(inFlightMutex);
    for (const auto &job : jobs)
    {
        inFlight.erase(workKeyFor(request, job));
    }
}

void TileRuntime::discardInFlightExcept(const FormulaSemanticsHash semanticsHash)
{
    std::lock_guard lock(inFlightMutex);
    std::erase_if(inFlight, [semanticsHash](const WorkKey &key)
    {
        return key.semanticsHash != semanticsHash;
    });
}

size_t TileRuntime::defaultWorkerCount()
{
    const auto hardware = std::thread::hardware_concurrency();
    if (hardware <= 2)
    {
        return 1;
    }
    return std::min<size_t>(hardware - 1, 4);
}

TileRuntime::WorkKey TileRuntime::workKeyFor(const ViewportRequest &request, const TileJob &job)
{
    return {
        .tile = job.key,
        .semanticsHash = request.formula.semanticsHash,
        .kind = job.kind
    };
}
}
