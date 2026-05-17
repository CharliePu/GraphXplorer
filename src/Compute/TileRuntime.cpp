#include "TileRuntime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <iterator>
#include <limits>
#include <map>
#include <thread>
#include <utility>

#include "../Tile/TileMath.h"
#include "../Util/PerformanceProfiler.h"
#include "../Util/PipelineLog.h"

namespace gx
{
size_t TileRuntime::WorkKeyHash::operator()(const WorkKey &key) const noexcept
{
    auto hash = TileKeyHash{}(key.tile);
    hash ^= std::hash<uint64_t>{}(key.semanticsHash.value) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    hash ^= std::hash<int>{}(static_cast<int>(key.kind)) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

TileRuntime::TileRuntime(std::unique_ptr<ComputeBackend> nextBackend,
                         const size_t workerCount,
                         TileRuntimeOptions nextOptions)
    : backend{nextBackend ? std::move(nextBackend) : makeDefaultComputeBackend()},
      options{std::move(nextOptions)},
      batchOptimizer{options.batchOptimizer},
      workers{workerCount == 0 ? defaultWorkerCount() : workerCount}
{
    options.rasterPixelsPerAxis = std::max<uint32_t>(1, options.rasterPixelsPerAxis);
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

void TileRuntime::setGpuRasterAllowed(const bool allowed)
{
    gpuRasterAllowed.store(allowed, std::memory_order_release);
}

void TileRuntime::setCompletionCallback(CompletionCallback callback)
{
    std::lock_guard lock(completionCallbackMutex);
    completionCallback = std::move(callback);
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
    std::map<uint32_t, std::vector<TileJob>> rasterJobsByPixels;
    intervalJobs.reserve(jobs.size());
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
            rasterJobsByPixels[rasterPixelsPerAxisFor(*request, job.key)].push_back(job);
        }
    }

    if (!intervalJobs.empty())
    {
        enqueueBatches(*request, *formula, JobKind::ClassifyInterval, std::move(intervalJobs));
    }
    for (auto &[pixelsPerAxis, rasterJobs] : rasterJobsByPixels)
    {
        if (!rasterJobs.empty())
        {
            enqueueBatches(*request, *formula, JobKind::RasterizeRegion, std::move(rasterJobs), pixelsPerAxis);
        }
    }
}

TileRuntimeDrainResult TileRuntime::drainCompleted(TileCache &tileCache,
                                                   std::unordered_map<uint64_t, RegionOutput> &regionPayloads,
                                                   const std::chrono::microseconds applyBudget)
{
    GRAPHX_PROFILE_SCOPE("runtime.drainCompleted");
    TileRuntimeDrainResult result;
    const auto deadline = std::chrono::steady_clock::now() + applyBudget;
    auto drained = completed.drainForFrame();
    const auto deferRemaining = [&](const size_t workIndex, const size_t transactionIndex)
    {
        for (auto index = drained.size(); index-- > workIndex + 1;)
        {
            completed.pushFront(std::move(drained[index]));
        }

        TileWorkResult deferred;
        auto &current = drained[workIndex];
        if (transactionIndex < current.transactions.size())
        {
            std::move(
                current.transactions.begin() + static_cast<std::ptrdiff_t>(transactionIndex),
                current.transactions.end(),
                std::back_inserter(deferred.transactions));
        }
        if (!deferred.transactions.empty())
        {
            completed.pushFront(std::move(deferred));
        }
    };

    for (size_t workIndex = 0; workIndex < drained.size(); ++workIndex)
    {
        auto &work = drained[workIndex];
        for (auto &region : work.regions)
        {
            regionPayloads[region.first] = std::move(region.second);
        }

        for (size_t transactionIndex = 0; transactionIndex < work.transactions.size(); ++transactionIndex)
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                deferRemaining(workIndex, transactionIndex);
                return result;
            }

            auto &transaction = work.transactions[transactionIndex];
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
                               std::vector<TileJob> jobs,
                               const uint32_t rasterPixelsPerAxis)
{
    workers.addTask([this, request, formula, kind, rasterPixelsPerAxis, jobs = std::move(jobs)]()
    {
        GRAPHX_PROFILE_SCOPE("runtime.workerBatch");
        const auto started = std::chrono::steady_clock::now();
        struct InFlightCleanup
        {
            TileRuntime *runtime{nullptr};
            const ViewportRequest &request;
            const std::vector<TileJob> &jobs;

            ~InFlightCleanup()
            {
                runtime->removeInFlight(request, jobs);
            }
        } cleanup{this, request, jobs};

        if (!isCurrent(request))
        {
            return;
        }

        TileWorkResult work;
        try
        {
            if (kind == JobKind::ClassifyInterval)
            {
                work = classifyTiles(request, formula, jobs);
            }
            else if (kind == JobKind::RasterizeRegion)
            {
                work = rasterizeTiles(request, formula, jobs, rasterPixelsPerAxis);
            }
        }
        catch (const std::exception &exception)
        {
            PipelineLog::log(
                "runtime.worker exception kind=%d jobs=%zu reason=%s",
                static_cast<int>(kind),
                jobs.size(),
                exception.what());
            work = recoveryWork(request, kind, jobs);
        }
        catch (...)
        {
            PipelineLog::log(
                "runtime.worker exception kind=%d jobs=%zu reason=<unknown>",
                static_cast<int>(kind),
                jobs.size());
            work = recoveryWork(request, kind, jobs);
        }

        if (isCurrent(request) && !work.transactions.empty())
        {
            completed.push(std::move(work));
            notifyCompletedWork();
        }

        batchOptimizer.observe(
            kind,
            jobs.size(),
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started),
            rasterPixelsPerAxis);
    });
}

void TileRuntime::enqueueBatches(const ViewportRequest &request,
                                 const CompiledFormula &formula,
                                 const JobKind kind,
                                 std::vector<TileJob> jobs,
                                 const uint32_t rasterPixelsPerAxis)
{
    auto offset = size_t{0};
    while (offset < jobs.size())
    {
        const auto remaining = jobs.size() - offset;
        const auto batchSize = std::max<size_t>(1, batchOptimizer.choose(kind, remaining, rasterPixelsPerAxis));
        const auto end = std::min(jobs.size(), offset + batchSize);
        std::vector<TileJob> batch;
        batch.reserve(end - offset);
        std::move(
            jobs.begin() + static_cast<std::ptrdiff_t>(offset),
            jobs.begin() + static_cast<std::ptrdiff_t>(end),
            std::back_inserter(batch));
        enqueueBatch(request, formula, kind, std::move(batch), rasterPixelsPerAxis);
        offset = end;
    }
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

    if (!batchResult.ok)
    {
        PipelineLog::log(
            "runtime.classify failed batch=%zu completed=%zu reason=%s",
            jobs.size(),
            batchResult.completed,
            batchResult.message.empty() ? "(none)" : batchResult.message.c_str());
        return recoveryWork(request, JobKind::ClassifyInterval, jobs);
    }
    if (!isCurrent(request))
    {
        return work;
    }

    const auto completedCount = std::min(batchResult.completed, classifications.size());
    work.transactions.reserve(jobs.size());
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
    if (completedCount < jobs.size())
    {
        PipelineLog::log(
            "runtime.classify incomplete batch=%zu completed=%zu",
            jobs.size(),
            completedCount);
        appendRecoveryTransactions(
            work,
            request,
            JobKind::ClassifyInterval,
            jobs.subspan(completedCount));
    }
    return work;
}

TileRuntime::TileWorkResult TileRuntime::rasterizeTiles(const ViewportRequest &request,
                                                        const CompiledFormula &formula,
                                                        const std::span<const TileJob> jobs,
                                                        const uint32_t pixelsPerAxis)
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
    const auto pixelsPerTile = pixelsPerAxis * pixelsPerAxis;
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
                pixelsPerAxis,
                gpuRasterAllowed.load(std::memory_order_acquire),
                [this, request]() { return !isCurrent(request); }
            },
            outputs);
    }

    if (!batchResult.ok)
    {
        PipelineLog::log(
            "runtime.raster failed batch=%zu completed=%zu pixels=%u reason=%s",
            jobs.size(),
            batchResult.completed,
            pixelsPerAxis,
            batchResult.message.empty() ? "(none)" : batchResult.message.c_str());
        return recoveryWork(request, JobKind::RasterizeRegion, jobs);
    }
    if (!isCurrent(request))
    {
        return work;
    }

    const auto completedCount = std::min(batchResult.completed, outputs.size());
    work.transactions.reserve(jobs.size());
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
    if (completedCount < jobs.size())
    {
        PipelineLog::log(
            "runtime.raster incomplete batch=%zu completed=%zu pixels=%u",
            jobs.size(),
            completedCount,
            pixelsPerAxis);
        appendRecoveryTransactions(
            work,
            request,
            JobKind::RasterizeRegion,
            jobs.subspan(completedCount));
    }
    return work;
}

TileRuntime::TileWorkResult TileRuntime::recoveryWork(const ViewportRequest &request,
                                                      const JobKind kind,
                                                      const std::span<const TileJob> jobs)
{
    TileWorkResult work;
    appendRecoveryTransactions(work, request, kind, jobs);
    return work;
}

void TileRuntime::appendRecoveryTransactions(TileWorkResult &work,
                                             const ViewportRequest &request,
                                             const JobKind kind,
                                             const std::span<const TileJob> jobs)
{
    work.transactions.reserve(work.transactions.size() + jobs.size());
    for (const auto &job : jobs)
    {
        TileDelta delta{
            .header = request.header,
            .semanticsHash = request.formula.semanticsHash,
            .key = job.key,
            .stage = TileStage::Evicted,
            .classification = TileClassification::Unknown
        };

        if (kind == JobKind::ClassifyInterval)
        {
            delta.stage = TileStage::Unknown;
        }
        else if (kind == JobKind::RasterizeRegion && job.interval)
        {
            delta.stage = TileStage::MixedNeedsRegion;
            delta.classification = TileClassification::Mixed;
            delta.interval = job.interval;
        }

        work.transactions.push_back(TileTransaction{
            .header = request.header,
            .semanticsHash = request.formula.semanticsHash,
            .deltas = {std::move(delta)}
        });
    }
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

void TileRuntime::notifyCompletedWork()
{
    CompletionCallback callback;
    {
        std::lock_guard lock(completionCallbackMutex);
        callback = completionCallback;
    }
    if (callback)
    {
        callback();
    }
}

size_t TileRuntime::recommendedWorkerCount(const size_t hardwareThreads,
                                           const size_t headroom,
                                           const size_t maxWorkers)
{
    const auto available = hardwareThreads > headroom ? hardwareThreads - headroom : size_t{1};
    const auto bounded = std::max<size_t>(1, available);
    return maxWorkers > 0 ? std::min(bounded, std::max<size_t>(1, maxWorkers)) : bounded;
}

size_t TileRuntime::defaultWorkerCount() const
{
    const auto hardware = std::thread::hardware_concurrency();
    return recommendedWorkerCount(hardware, options.cpuWorkerHeadroom, options.maxWorkerCount);
}

TileRuntime::WorkKey TileRuntime::workKeyFor(const ViewportRequest &request, const TileJob &job)
{
    return {
        .tile = job.key,
        .semanticsHash = request.formula.semanticsHash,
        .kind = job.kind
    };
}

uint32_t TileRuntime::rasterPixelsPerAxisFor(const ViewportRequest &request, const TileKey &key) const
{
    (void)request;
    (void)key;
    return std::max<uint32_t>(1, options.rasterPixelsPerAxis);
}
}
