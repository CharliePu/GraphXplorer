//
// Created by charl on 6/3/2024.
//

#include "GraphProcessor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "../Core/Window.h"
#include "../Formula/Formula.h"
#include "../Graph/Graph.h"

GraphProcessor::GraphProcessor(const std::shared_ptr<Window> &window,
                               const std::shared_ptr<ThreadPool> &threadPool): window{window}, threadPool{threadPool}
{
}

void GraphProcessor::process(const std::shared_ptr<Graph> &graph, const std::shared_ptr<Formula> &formula,
                             const Interval &xRange, const Interval &yRange, const int windowWidth,
                             const int windowHeight, const CancelFn &cancelled)
{
    if (!graph)
    {
        throw std::invalid_argument("Graph must not be null");
    }

    if (!formula)
    {
        throw std::invalid_argument("Formula must not be null");
    }

    if (cancelled && cancelled())
    {
        return;
    }

    const auto level = targetLevel(xRange, yRange, windowWidth, windowHeight);

    auto rootLevel = clampChunkLevel(getCoarsestViewportLevel(xRange, yRange));
    rootLevel = std::max(rootLevel, level);

    const auto [minChunkX, maxChunkX] = chunkIndexBounds(xRange, rootLevel);
    const auto [minChunkY, maxChunkY] = chunkIndexBounds(yRange, rootLevel);

    for (auto chunkY = minChunkY; chunkY <= maxChunkY; ++chunkY)
    {
        if (cancelled && cancelled())
        {
            return;
        }

        for (auto chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX)
        {
            if (cancelled && cancelled())
            {
                return;
            }

            refineTile(graph, formula, chunkX, chunkY, rootLevel, level, xRange, yRange, cancelled);
        }
    }
}

int GraphProcessor::getCoarsestViewportLevel(const Interval &xRange, const Interval &yRange)
{
    const auto maxRangeSize = std::max(xRange.size(), yRange.size());

    if (maxRangeSize <= 0.0)
    {
        return 0;
    }

    return clampChunkLevel(static_cast<int>(std::ceil(std::log2(maxRangeSize))));
}

bool GraphProcessor::intersects(const Interval &lhs, const Interval &rhs)
{
    return lhs.lower < rhs.upper && rhs.lower < lhs.upper;
}

Tile &GraphProcessor::getOrComputeTile(const std::shared_ptr<Graph> &graph, const std::shared_ptr<Formula> &formula,
                                       const int64_t chunkX, const int64_t chunkY, const int level)
{
    const TileKey key{chunkX, chunkY, level};

    const auto [it, inserted] = graph->tiles.try_emplace(key, Tile{});
    if (inserted)
    {
        const auto xTileRange = chunkIndexToRange(chunkX, level);
        const auto yTileRange = chunkIndexToRange(chunkY, level);

        it->second.solution = formula->evaluate({{"x", xTileRange}, {"y", yTileRange}});
        graph->activeLevels.insert(level);
    }

    return it->second;
}

void GraphProcessor::refineTile(const std::shared_ptr<Graph> &graph, const std::shared_ptr<Formula> &formula,
                                const int64_t chunkX, const int64_t chunkY, const int level, const int targetLevel,
                                const Interval &viewXRange, const Interval &viewYRange,
                                const CancelFn &cancelled) const
{
    if (cancelled && cancelled())
    {
        return;
    }

    auto &tile = getOrComputeTile(graph, formula, chunkX, chunkY, level);

    if (tile.solution.allTrue() || tile.solution.allFalse() || level <= targetLevel)
    {
        return;
    }

    const auto childLevel = level - 1;
    const auto firstChildX = chunkX * 2;
    const auto firstChildY = chunkY * 2;

    for (auto yOffset = int64_t{0}; yOffset < 2; ++yOffset)
    {
        if (cancelled && cancelled())
        {
            return;
        }

        for (auto xOffset = int64_t{0}; xOffset < 2; ++xOffset)
        {
            if (cancelled && cancelled())
            {
                return;
            }

            const auto childX = firstChildX + xOffset;
            const auto childY = firstChildY + yOffset;

            const auto childXRange = chunkIndexToRange(childX, childLevel);
            const auto childYRange = chunkIndexToRange(childY, childLevel);

            if (!intersects(childXRange, viewXRange) || !intersects(childYRange, viewYRange))
            {
                continue;
            }

            refineTile(graph, formula, childX, childY, childLevel, targetLevel, viewXRange, viewYRange, cancelled);
        }
    }
}
