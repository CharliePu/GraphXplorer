#include "TilePlanner.h"

#include <algorithm>
#include <cmath>

#include "../Tile/TileMath.h"

namespace gx
{
TilePlan TilePlanner::plan(const ViewportRequest &request,
                           TileCache &tileCache,
                           const TilePlanBudget &budget,
                           const int maxSeedCells,
                           const int refinementDepth) const
{
    TilePlan result;
    if (!request.valid() || maxSeedCells <= 0)
    {
        return result;
    }

    const auto level = seedTileLevelForViewport(request, maxSeedCells);
    const auto [minX, maxX] = tileIndexBounds(request.xRange, level);
    const auto [minY, maxY] = tileIndexBounds(request.yRange, level);
    const auto width = maxX - minX + 1;
    const auto height = maxY - minY + 1;
    if (width <= 0 || height <= 0 || width * height > maxSeedCells)
    {
        return result;
    }

    BudgetState budgetState{
        .interval = budget.maxIntervalJobsPerFrame,
        .raster = budget.maxRasterJobsPerFrame
    };
    const auto leafLevel = leafTileLevelForSeed(level, refinementDepth);

    for (auto y = minY; y <= maxY; ++y)
    {
        for (auto x = minX; x <= maxX; ++x)
        {
            visitAuthority(request,
                           tileCache,
                           TileKey{x, y, level},
                           leafLevel,
                           result,
                           budgetState);
        }
    }

    std::ranges::sort(result.jobs, [](const TileJob &lhs, const TileJob &rhs)
    {
        if (lhs.priority != rhs.priority)
        {
            return lhs.priority < rhs.priority;
        }
        return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
    });
    return result;
}

void TilePlanner::visitAuthority(const ViewportRequest &request,
                                 TileCache &tileCache,
                                 const TileKey &key,
                                 const int leafLevel,
                                 TilePlan &plan,
                                 BudgetState &budget)
{
    if (!intersects(tileBounds(key), request.xRange, request.yRange))
    {
        return;
    }

    if (const auto *uniform = tileCache.findNearestUniformAncestorOrSelf(key, request.formula.semanticsHash))
    {
        if (uniform->key != key && tileCache.find(key, request.formula.semanticsHash))
        {
            if (tileCache.erase(key, request.formula.semanticsHash))
            {
                plan.erasedShadowedTiles.push_back(key);
            }
        }
        return;
    }

    const auto *record = tileCache.find(key, request.formula.semanticsHash);
    if (!record)
    {
        (void)tileCache.getOrCreate(key, request.formula.semanticsHash);
        enqueueClassifyIfIdle(plan, budget, request, tileCache, key);
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
            for (const auto &child : tileChildren(key))
            {
                visitAuthority(request, tileCache, child, leafLevel, plan, budget);
            }
            return;
        }

        enqueueRasterIfIdle(plan, budget, request, tileCache, key, *record);
        return;
    }

    enqueueClassifyIfIdle(plan, budget, request, tileCache, key);
}

void TilePlanner::enqueueClassifyIfIdle(TilePlan &plan,
                                        BudgetState &budget,
                                        const ViewportRequest &request,
                                        TileCache &tileCache,
                                        const TileKey &key)
{
    const auto *record = tileCache.find(key, request.formula.semanticsHash);
    if (!record
        || record->valueState != TileValueState::Unknown
        || record->workState != TileWorkState::Idle
        || budget.interval <= 0)
    {
        return;
    }

    if (!tileCache.transition(key, request.formula.semanticsHash, TileStage::IntervalQueued))
    {
        return;
    }

    --budget.interval;
    plan.jobs.push_back({
        JobKind::ClassifyInterval,
        WorkClass::VisibleNow,
        key,
        priorityFor(request, key),
        {}
    });
}

void TilePlanner::enqueueRasterIfIdle(TilePlan &plan,
                                      BudgetState &budget,
                                      const ViewportRequest &request,
                                      TileCache &tileCache,
                                      const TileKey &key,
                                      const TileRecord &record)
{
    if (record.valueState != TileValueState::Mixed
        || record.workState != TileWorkState::Idle
        || budget.raster <= 0)
    {
        return;
    }

    if (!tileCache.transition(key, request.formula.semanticsHash, TileStage::RegionQueued))
    {
        return;
    }

    --budget.raster;
    plan.jobs.push_back(TileJob{
        .kind = JobKind::RasterizeRegion,
        .workClass = WorkClass::VisibleRefinement,
        .key = key,
        .priority = priorityFor(request, key),
        .dependencies = {.interval = true},
        .interval = record.interval
    });
}

int TilePlanner::priorityFor(const ViewportRequest &request, const TileKey &key)
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
}
