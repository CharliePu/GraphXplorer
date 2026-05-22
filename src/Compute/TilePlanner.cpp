#include "TilePlanner.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../Tile/TileMath.h"
#include "../Util/PerformanceProfiler.h"
#include "../Util/ThreadPool.h"

namespace gx
{
namespace
{
enum class PlannerCandidateKind
{
    EraseShadowed,
    ClassifyInterval,
    RasterizeRegion
};

struct PlannerCandidate
{
    PlannerCandidateKind kind{PlannerCandidateKind::ClassifyInterval};
    TileKey key{};
    int priority{0};
    TexturePreparationMode textureMode{TexturePreparationMode::GpuPreview};
};

struct CandidateIdentity
{
    PlannerCandidateKind kind{PlannerCandidateKind::ClassifyInterval};
    TileKey key{};

    bool operator==(const CandidateIdentity &) const = default;
};

struct CandidateIdentityHash
{
    size_t operator()(const CandidateIdentity &identity) const noexcept
    {
        auto hash = TileKeyHash{}(identity.key);
        hash ^= std::hash<int>{}(static_cast<int>(identity.kind)) + 0x9e3779b97f4a7c15ull + (hash << 6)
            + (hash >> 2);
        return hash;
    }
};

struct BudgetState
{
    int interval{0};
    int raster{0};
};

struct DiscoveryCounters
{
    size_t visitedTiles{0};
    size_t offloadedTasks{0};
    size_t inlineTasks{0};
};

struct PlannerDiscoveryResult
{
    std::vector<PlannerCandidate> candidates{};
    TilePlanStats stats{};
};

[[nodiscard]] bool renderReadyUniform(const TileRecord &record)
{
    return record.valueState == TileValueState::UniformTrue
        || record.valueState == TileValueState::UniformFalse;
}

[[nodiscard]] int priorityFor(const ViewportRequest &request, const TileKey &key)
{
    const auto bounds = tileBounds(key);
    const auto centerX = (bounds.xMin + bounds.xMax) * 0.5;
    const auto centerY = (bounds.yMin + bounds.yMax) * 0.5;
    const auto requestCenterX = request.xRange.mid();
    const auto requestCenterY = request.yRange.mid();
    const auto dx = centerX - requestCenterX;
    const auto dy = centerY - requestCenterY;
    const auto distance = std::sqrt(dx * dx + dy * dy);
    return static_cast<int>(distance * 1000.0);
}

[[nodiscard]] TileKey ancestorAtLevel(const TileKey &key, const int level)
{
    if (level <= key.level)
    {
        return key;
    }

    const auto shift = level - key.level;
    return {
        floorDivByPow2(key.x, shift),
        floorDivByPow2(key.y, shift),
        level
    };
}

[[nodiscard]] int sortRank(const PlannerCandidateKind kind)
{
    switch (kind)
    {
    case PlannerCandidateKind::EraseShadowed:
        return 0;
    case PlannerCandidateKind::ClassifyInterval:
        return 1;
    case PlannerCandidateKind::RasterizeRegion:
        return 2;
    }
    return 3;
}

[[nodiscard]] bool candidateLess(const PlannerCandidate &lhs, const PlannerCandidate &rhs)
{
    if (lhs.priority != rhs.priority)
    {
        return lhs.priority < rhs.priority;
    }
    if (lhs.kind != rhs.kind)
    {
        return sortRank(lhs.kind) < sortRank(rhs.kind);
    }
    if (lhs.key.level != rhs.key.level)
    {
        return lhs.key.level > rhs.key.level;
    }
    if (lhs.key.y != rhs.key.y)
    {
        return lhs.key.y < rhs.key.y;
    }
    return lhs.key.x < rhs.key.x;
}

[[nodiscard]] bool jobLess(const TileJob &lhs, const TileJob &rhs)
{
    if (lhs.priority != rhs.priority)
    {
        return lhs.priority < rhs.priority;
    }
    if (lhs.kind != rhs.kind)
    {
        return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
    }
    if (lhs.key.level != rhs.key.level)
    {
        return lhs.key.level > rhs.key.level;
    }
    if (lhs.key.y != rhs.key.y)
    {
        return lhs.key.y < rhs.key.y;
    }
    return lhs.key.x < rhs.key.x;
}

class TilePlannerSnapshot
{
public:
    explicit TilePlannerSnapshot(std::vector<TileRecord> records)
    {
        entries.reserve(records.size());
        occupiedLevels.reserve(records.size());
        for (auto &record : records)
        {
            occupiedLevels.push_back(record.key.level);
            entries.insert_or_assign(record.key, std::move(record));
        }

        std::ranges::sort(occupiedLevels);
        const auto duplicates = std::ranges::unique(occupiedLevels);
        occupiedLevels.erase(duplicates.begin(), duplicates.end());
    }

    [[nodiscard]] const TileRecord *find(const TileKey &key) const
    {
        const auto it = entries.find(key);
        return it == entries.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const TileRecord *findNearestUniformAncestorOrSelf(const TileKey &key) const
    {
        for (auto levelIt = occupiedLevels.rbegin(); levelIt != occupiedLevels.rend(); ++levelIt)
        {
            const auto level = *levelIt;
            if (level < key.level)
            {
                continue;
            }

            const auto ancestor = ancestorAtLevel(key, level);
            const auto *record = find(ancestor);
            if (record && renderReadyUniform(*record))
            {
                return record;
            }
        }
        return nullptr;
    }

private:
    std::unordered_map<TileKey, TileRecord, TileKeyHash> entries;
    std::vector<int> occupiedLevels;
};

[[nodiscard]] std::vector<TileKey> seedTilesForRequest(
    const ViewportRequest &request,
    const int maxSeedCells,
    int &seedLevel)
{
    std::vector<TileKey> seeds;
    if (!request.valid() || maxSeedCells <= 0)
    {
        return seeds;
    }

    seedLevel = seedTileLevelForViewport(request, maxSeedCells);
    const auto [minX, maxX] = tileIndexBounds(request.xRange, seedLevel);
    const auto [minY, maxY] = tileIndexBounds(request.yRange, seedLevel);
    const auto width = maxX - minX + 1;
    const auto height = maxY - minY + 1;
    if (width <= 0 || height <= 0 || width * height > maxSeedCells)
    {
        seeds.clear();
        return seeds;
    }

    seeds.reserve(static_cast<size_t>(width * height));
    for (auto y = minY; y <= maxY; ++y)
    {
        for (auto x = minX; x <= maxX; ++x)
        {
            seeds.push_back(TileKey{x, y, seedLevel});
        }
    }
    return seeds;
}

class PlannerDiscoveryRun : public std::enable_shared_from_this<PlannerDiscoveryRun>
{
public:
    PlannerDiscoveryRun(ViewportRequest nextRequest,
                        std::shared_ptr<const TilePlannerSnapshot> nextSnapshot,
                        const int nextLeafLevel,
                        ThreadPool *nextWorkers)
        : request{std::move(nextRequest)},
          snapshot{std::move(nextSnapshot)},
          leafLevel{nextLeafLevel},
          workers{nextWorkers}
    {
    }

    [[nodiscard]] PlannerDiscoveryResult discover(const std::span<const TileKey> seeds)
    {
        std::vector<TileKey> localStack;
        localStack.reserve(seeds.size());
        DiscoveryCounters localCounters;
        for (const auto &seed : seeds)
        {
            if (!trySpawn(seed, localCounters))
            {
                localStack.push_back(seed);
            }
        }

        std::vector<PlannerCandidate> local;
        processStack(std::move(localStack), local, localCounters);
        publish(std::move(local), localCounters);
        waitForSpawnedTasks();

        PlannerDiscoveryResult result;
        {
            std::lock_guard lock(resultsMutex);
            size_t total = 0;
            for (const auto &chunk : resultChunks)
            {
                total += chunk.candidates.size();
            }
            result.candidates.reserve(total);
            for (auto &chunk : resultChunks)
            {
                result.stats.visitedTiles += chunk.counters.visitedTiles;
                result.stats.offloadedTasks += chunk.counters.offloadedTasks;
                result.stats.inlineTasks += chunk.counters.inlineTasks;
                std::move(
                    chunk.candidates.begin(),
                    chunk.candidates.end(),
                    std::back_inserter(result.candidates));
            }
            result.stats.resultChunks = resultChunks.size();
        }
        result.stats.candidatesDiscovered = result.candidates.size();
        return result;
    }

private:
    struct ResultChunk
    {
        std::vector<PlannerCandidate> candidates{};
        DiscoveryCounters counters{};
    };

    void processStack(std::vector<TileKey> stack,
                      std::vector<PlannerCandidate> &out,
                      DiscoveryCounters &counters)
    {
        while (!stack.empty())
        {
            const auto key = stack.back();
            stack.pop_back();
            visit(key, stack, out, counters);
        }
    }

    void visit(const TileKey &key,
               std::vector<TileKey> &stack,
               std::vector<PlannerCandidate> &out,
               DiscoveryCounters &counters)
    {
        ++counters.visitedTiles;
        if (!intersects(tileBounds(key), request.xRange, request.yRange))
        {
            return;
        }

        if (const auto *uniform = snapshot->findNearestUniformAncestorOrSelf(key))
        {
            if (uniform->key != key && snapshot->find(key))
            {
                out.push_back({
                    .kind = PlannerCandidateKind::EraseShadowed,
                    .key = key,
                    .priority = priorityFor(request, key)
                });
            }
            return;
        }

        const auto *record = snapshot->find(key);
        if (!record)
        {
            out.push_back({
                .kind = PlannerCandidateKind::ClassifyInterval,
                .key = key,
                .priority = priorityFor(request, key)
            });
            return;
        }

        if (record->valueState == TileValueState::UniformTrue
            || record->valueState == TileValueState::UniformFalse)
        {
            return;
        }

        if (record->valueState == TileValueState::Mixed)
        {
            if (key.level > leafLevel)
            {
                if (record->workState == TileWorkState::Idle)
                {
                    out.push_back({
                        .kind = PlannerCandidateKind::RasterizeRegion,
                        .key = key,
                        .priority = priorityFor(request, key)
                    });
                }
                const auto children = tileChildren(key);
                for (auto index = children.size(); index-- > 0;)
                {
                    if (!trySpawn(children[index], counters))
                    {
                        stack.push_back(children[index]);
                    }
                }
                return;
            }

            if (record->workState == TileWorkState::Idle)
            {
                out.push_back({
                    .kind = PlannerCandidateKind::RasterizeRegion,
                    .key = key,
                    .priority = priorityFor(request, key)
                });
            }
            else if (record->workState == TileWorkState::RegionReady
                     && record->regionPixels
                     && record->regionPixels->certainty == TextureCertainty::Imprecise)
            {
                out.push_back({
                    .kind = PlannerCandidateKind::RasterizeRegion,
                    .key = key,
                    .priority = priorityFor(request, key),
                    .textureMode = TexturePreparationMode::Refined
                });
            }
            return;
        }

        if (record->valueState == TileValueState::Unknown
            && record->workState == TileWorkState::Idle)
        {
            out.push_back({
                .kind = PlannerCandidateKind::ClassifyInterval,
                .key = key,
                .priority = priorityFor(request, key)
            });
        }
    }

    [[nodiscard]] bool trySpawn(const TileKey &key, DiscoveryCounters &counters)
    {
        if (!workers)
        {
            return false;
        }

        activeTasks.fetch_add(1, std::memory_order_relaxed);
        auto self = shared_from_this();
        bool accepted = false;
        try
        {
            accepted = workers->tryAddTask([self, key]()
            {
                self->runSpawnedTask(key);
            });
        }
        catch (...)
        {
            activeTasks.fetch_sub(1, std::memory_order_relaxed);
            return false;
        }
        if (!accepted)
        {
            activeTasks.fetch_sub(1, std::memory_order_relaxed);
            ++counters.inlineTasks;
            return false;
        }
        ++counters.offloadedTasks;
        return accepted;
    }

    void runSpawnedTask(const TileKey &key)
    {
        struct Completion
        {
            PlannerDiscoveryRun &run;

            ~Completion()
            {
                run.finishSpawnedTask();
            }
        } completion{*this};

        try
        {
            DiscoveryCounters counters;
            std::vector<PlannerCandidate> local;
            processStack({key}, local, counters);
            publish(std::move(local), counters);
        }
        catch (...)
        {
        }
    }

    void finishSpawnedTask()
    {
        if (activeTasks.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            doneCv.notify_all();
        }
    }

    void publish(std::vector<PlannerCandidate> candidates, const DiscoveryCounters counters)
    {
        if (candidates.empty() && counters.visitedTiles == 0)
        {
            return;
        }

        std::lock_guard lock(resultsMutex);
        resultChunks.push_back(ResultChunk{
            .candidates = std::move(candidates),
            .counters = counters
        });
    }

    void waitForSpawnedTasks()
    {
        std::unique_lock lock(doneMutex);
        doneCv.wait(lock, [this]()
        {
            return activeTasks.load(std::memory_order_acquire) == 0;
        });
    }

    ViewportRequest request;
    std::shared_ptr<const TilePlannerSnapshot> snapshot;
    int leafLevel{0};
    ThreadPool *workers{nullptr};
    std::atomic<size_t> activeTasks{0};
    std::mutex resultsMutex;
    std::vector<ResultChunk> resultChunks;
    std::mutex doneMutex;
    std::condition_variable doneCv;
};

void commitEraseShadowed(TilePlan &plan,
                         TileCache &tileCache,
                         const ViewportRequest &request,
                         const TileKey &key)
{
    const auto *uniform = tileCache.findNearestUniformAncestorOrSelf(key, request.formula.semanticsHash);
    if (!uniform || uniform->key == key || !tileCache.find(key, request.formula.semanticsHash))
    {
        return;
    }

    if (tileCache.erase(key, request.formula.semanticsHash))
    {
        plan.erasedShadowedTiles.push_back(key);
    }
}

void commitClassifyCandidate(TilePlan &plan,
                             BudgetState &budget,
                             TileCache &tileCache,
                             const ViewportRequest &request,
                             const PlannerCandidate &candidate)
{
    if (budget.interval <= 0)
    {
        return;
    }

    const auto *record = tileCache.find(candidate.key, request.formula.semanticsHash);
    if (record
        && (record->valueState != TileValueState::Unknown
            || record->workState != TileWorkState::Idle))
    {
        return;
    }

    if (!tileCache.transition(candidate.key, request.formula.semanticsHash, TileStage::IntervalQueued))
    {
        return;
    }

    --budget.interval;
    plan.jobs.push_back({
        JobKind::ClassifyInterval,
        WorkClass::VisibleNow,
        candidate.key,
        candidate.priority,
        {}
    });
}

void commitRasterCandidate(TilePlan &plan,
                           BudgetState &budget,
                           TileCache &tileCache,
                           const ViewportRequest &request,
                           const PlannerCandidate &candidate)
{
    if (budget.raster <= 0)
    {
        return;
    }

    const auto *record = tileCache.find(candidate.key, request.formula.semanticsHash);
    if (!record
        || record->valueState != TileValueState::Mixed)
    {
        return;
    }
    const auto canStartPreview = record->workState == TileWorkState::Idle
        && candidate.textureMode == TexturePreparationMode::GpuPreview;
    const auto canStartProof = record->workState == TileWorkState::RegionReady
        && candidate.textureMode == TexturePreparationMode::Refined
        && record->regionPixels
        && record->regionPixels->certainty == TextureCertainty::Imprecise;
    if (!canStartPreview && !canStartProof)
    {
        return;
    }

    const auto interval = record->interval;
    if (!tileCache.transition(candidate.key, request.formula.semanticsHash, TileStage::RegionQueued))
    {
        return;
    }

    --budget.raster;
    plan.jobs.push_back(TileJob{
        .kind = JobKind::RasterizeRegion,
        .workClass = WorkClass::VisibleRefinement,
        .key = candidate.key,
        .priority = candidate.priority,
        .dependencies = {.interval = true},
        .interval = interval,
        .textureMode = candidate.textureMode
    });
}

[[nodiscard]] TilePlan commitCandidates(const ViewportRequest &request,
                                        TileCache &tileCache,
                                        std::vector<PlannerCandidate> candidates,
                                        const TilePlanBudget &budget,
                                        TilePlanStats stats)
{
    TilePlan plan;
    std::ranges::sort(candidates, candidateLess);

    BudgetState budgetState{
        .interval = budget.maxIntervalJobsPerFrame,
        .raster = budget.maxRasterJobsPerFrame
    };
    std::unordered_set<CandidateIdentity, CandidateIdentityHash> committed;
    committed.reserve(candidates.size());

    for (const auto &candidate : candidates)
    {
        if (!committed.insert(CandidateIdentity{candidate.kind, candidate.key}).second)
        {
            continue;
        }

        switch (candidate.kind)
        {
        case PlannerCandidateKind::EraseShadowed:
            commitEraseShadowed(plan, tileCache, request, candidate.key);
            break;
        case PlannerCandidateKind::ClassifyInterval:
            commitClassifyCandidate(plan, budgetState, tileCache, request, candidate);
            break;
        case PlannerCandidateKind::RasterizeRegion:
            commitRasterCandidate(plan, budgetState, tileCache, request, candidate);
            break;
        }
    }

    std::ranges::sort(plan.jobs, jobLess);
    stats.committedCandidates = plan.jobs.size() + plan.erasedShadowedTiles.size();
    plan.stats = stats;
    return plan;
}
}

TilePlan TilePlanner::plan(const ViewportRequest &request,
                           TileCache &tileCache,
                           const TilePlanBudget &budget,
                           const int maxSeedCells,
                           const int refinementDepth,
                           ThreadPool *workers) const
{
    GRAPHX_PROFILE_SCOPE("tilePlanner.plan");
    const auto planStart = std::chrono::steady_clock::now();
    int seedLevel = 0;
    auto seeds = seedTilesForRequest(request, maxSeedCells, seedLevel);
    if (seeds.empty())
    {
        return {};
    }

    const auto leafLevel = leafTileLevelForSeed(seedLevel, refinementDepth);
    auto records = tileCache.recordsForFormula(request.formula.semanticsHash);
    const auto snapshotRecords = records.size();
    auto snapshot = std::make_shared<TilePlannerSnapshot>(std::move(records));
    auto discovery = std::make_shared<PlannerDiscoveryRun>(
        request,
        std::move(snapshot),
        leafLevel,
        workers);
    const auto workerCount = workers ? workers->workerCount() : 0;
    const auto idleWorkersAtStart = workers ? workers->idleWorkerCount() : 0;
    const auto discoveryStart = std::chrono::steady_clock::now();
    auto discoveryResult = discovery->discover(seeds);
    const auto commitStart = std::chrono::steady_clock::now();
    auto stats = discoveryResult.stats;
    stats.seedTiles = seeds.size();
    stats.snapshotRecords = snapshotRecords;
    stats.workerCount = workerCount;
    stats.idleWorkersAtStart = idleWorkersAtStart;
    stats.parallelEnabled = workers != nullptr && stats.workerCount > 0;
    stats.discoveryTime = std::chrono::duration_cast<std::chrono::microseconds>(commitStart - discoveryStart);
    auto plan = commitCandidates(request, tileCache, std::move(discoveryResult.candidates), budget, stats);
    plan.stats.commitTime = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - commitStart);
    plan.stats.totalTime = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - planStart);
    return plan;
}
}
