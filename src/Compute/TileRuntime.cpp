#include "TileRuntime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <thread>
#include <utility>

#include "../Tile/TileMath.h"

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
    : backend{std::move(nextBackend)},
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

    for (const auto &job : jobs)
    {
        if (job.kind != JobKind::ClassifyInterval && job.kind != JobKind::RasterizeRegion)
        {
            continue;
        }
        enqueueJob(*request, *formula, job);
    }
}

TileRuntimeDrainResult TileRuntime::drainCompleted(TileCache &tileCache,
                                                   std::unordered_map<uint64_t, RegionOutput> &regionPayloads,
                                                   const std::chrono::microseconds applyBudget)
{
    (void)applyBudget;
    TileRuntimeDrainResult result;
    for (auto &work : completed.drainForFrame())
    {
        if (!work.transaction.deltas.empty())
        {
            if (!isCurrent(work.transaction.header, work.transaction.semanticsHash))
            {
                result.rejected += work.transaction.deltas.size();
                continue;
            }

            for (auto &region : work.regions)
            {
                regionPayloads[region.first] = std::move(region.second);
            }

            const auto applyResult = tileCache.apply(work.transaction);
            result.applied += applyResult.applied;
            result.rejected += applyResult.rejected;
            result.transactions.push_back(std::move(work.transaction));
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

void TileRuntime::enqueueJob(const ViewportRequest &request,
                             const CompiledFormula &formula,
                             const TileJob &job)
{
    const auto key = workKeyFor(request, job);
    {
        std::lock_guard lock(inFlightMutex);
        if (!inFlight.insert(key).second)
        {
            return;
        }
    }

    workers.addTask([this, request, formula, job, key]()
    {
        if (!isCurrent(request))
        {
            removeInFlight(key);
            return;
        }

        TileWorkResult work;
        if (job.kind == JobKind::ClassifyInterval)
        {
            work = classifyTile(request, formula, job);
        }
        else if (job.kind == JobKind::RasterizeRegion)
        {
            work = rasterizeTile(request, formula, job);
        }

        if (isCurrent(request) && !work.transaction.deltas.empty())
        {
            completed.push(std::move(work));
        }

        removeInFlight(key);
    });
}

TileRuntime::TileWorkResult TileRuntime::classifyTile(const ViewportRequest &request,
                                                      const CompiledFormula &formula,
                                                      const TileJob &job)
{
    TileWorkResult work;
    const auto bounds = tileBounds(job.key);
    std::array keys{job.key};
    std::array xMin{bounds.xMin};
    std::array xMax{bounds.xMax};
    std::array yMin{bounds.yMin};
    std::array yMax{bounds.yMax};
    std::array<TileClassificationResult, 1> classifications{};

    BatchResult batchResult;
    {
        std::lock_guard lock(backendMutex);
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

    const auto &classification = classifications.front();
    auto finalStage = TileStage::MixedNeedsRegion;
    if (classification.classification == TileClassification::UniformTrue)
    {
        finalStage = TileStage::UniformTrue;
    }
    else if (classification.classification == TileClassification::UniformFalse)
    {
        finalStage = TileStage::UniformFalse;
    }

    work.transaction = {
        .header = request.header,
        .semanticsHash = request.formula.semanticsHash
    };
    work.transaction.deltas.push_back({
        .header = request.header,
        .semanticsHash = request.formula.semanticsHash,
        .key = classification.key,
        .stage = TileStage::IntervalQueued,
        .classification = TileClassification::Unknown
    });
    work.transaction.deltas.push_back({
        .header = request.header,
        .semanticsHash = request.formula.semanticsHash,
        .key = classification.key,
        .stage = TileStage::IntervalReady,
        .classification = classification.classification,
        .interval = classification.interval
    });
    work.transaction.deltas.push_back({
        .header = request.header,
        .semanticsHash = request.formula.semanticsHash,
        .key = classification.key,
        .stage = finalStage,
        .classification = classification.classification,
        .interval = classification.interval
    });
    return work;
}

TileRuntime::TileWorkResult TileRuntime::rasterizeTile(const ViewportRequest &request,
                                                       const CompiledFormula &formula,
                                                       const TileJob &job)
{
    TileWorkResult work;
    const auto bounds = tileBounds(job.key);
    std::array keys{job.key};
    std::array xMin{bounds.xMin};
    std::array xMax{bounds.xMax};
    std::array yMin{bounds.yMin};
    std::array yMax{bounds.yMax};
    std::array<uint32_t, 1> offsets{0};
    std::array<RegionOutput, 1> outputs{};

    BatchResult batchResult;
    {
        std::lock_guard lock(backendMutex);
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

    const auto payloadId = nextRegionPayloadId.fetch_add(1, std::memory_order_relaxed);
    auto output = std::move(outputs.front());
    const auto width = static_cast<int>(output.width);
    const auto height = static_cast<int>(output.height);

    work.transaction = {
        .header = request.header,
        .semanticsHash = request.formula.semanticsHash
    };
    work.transaction.deltas.push_back({
        .header = request.header,
        .semanticsHash = request.formula.semanticsHash,
        .key = job.key,
        .stage = TileStage::RegionQueued,
        .classification = TileClassification::Mixed
    });
    work.transaction.deltas.push_back({
        .header = request.header,
        .semanticsHash = request.formula.semanticsHash,
        .key = job.key,
        .stage = TileStage::RegionReady,
        .classification = TileClassification::Mixed,
        .region = RegionImageRef{payloadId, width, height}
    });
    work.regions.emplace_back(payloadId, std::move(output));
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
