//
// Created by charl on 6/3/2024.
//

#include "GraphRasterizer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "CpuChunkRenderer.h"
#include "OpenCLChunkRenderer.h"
#include "../Core/Window.h"
#include "../Formula/Formula.h"
#include "../Graph/Graph.h"
#include "../Util/ThreadPool.h"

GraphRasterizer::GraphRasterizer(const std::shared_ptr<Window> &window,
                                 const std::shared_ptr<ThreadPool> &threadPool): window{window},
                                                                                  threadPool{threadPool},
                                                                                  chunkRenderer{nullptr},
                                                                                  usingGpuChunkRenderer{false},
                                                                                  cachedFormula{nullptr},
                                                                                  mixedChunkTextureCache{}
{
    auto gpuRenderer = std::make_unique<OpenCLChunkRenderer>();
    if (gpuRenderer->isAvailable())
    {
        chunkRenderer = std::move(gpuRenderer);
        usingGpuChunkRenderer = true;
        std::cout << "[GraphRasterizer] Mixed chunk renderer backend: OpenCL device" << std::endl;
    }
    else
    {
        chunkRenderer = std::make_unique<CpuChunkRenderer>();
        std::cout << "[GraphRasterizer] Mixed chunk renderer backend: CPU" << std::endl;
    }
}

int GraphRasterizer::getTargetLevel(const Interval &xRange, const Interval &yRange, const int windowWidth,
                                    const int windowHeight)
{
    const auto minRangeSize = std::min(xRange.size(), yRange.size());
    const auto maxWindowSize = std::max(windowWidth, windowHeight);

    if (minRangeSize <= 0.0 || maxWindowSize <= 0)
    {
        return 0;
    }

    const auto rangePerPixel = minRangeSize / static_cast<double>(maxWindowSize);
    const auto rangePerChunk = rangePerPixel * static_cast<double>(MIN_CHUNK_PIXELS);
    return static_cast<int>(std::floor(std::log2(rangePerChunk)));
}

std::pair<int64_t, int64_t> GraphRasterizer::getChunkIndexBounds(const Interval &range, const int level)
{
    if (range.upper <= range.lower)
    {
        const auto chunkIndex = worldToChunkIndex(range.lower, level);
        return {chunkIndex, chunkIndex};
    }

    const auto chunkSize = chunkSizeForLevel(level);
    const auto minIndex = worldToChunkIndex(range.lower, level);
    const auto scaledUpper = range.upper / chunkSize;
    const auto upperInclusiveScaled = std::nextafter(scaledUpper, -std::numeric_limits<double>::infinity());
    const auto maxIndex = static_cast<int64_t>(std::floor(upperInclusiveScaled));
    return {minIndex, maxIndex};
}

std::optional<Interval> GraphRasterizer::lookupAtLevel(const std::shared_ptr<Graph> &graph, const double x,
                                                       const double y, const int level)
{
    const auto chunkX = worldToChunkIndex(x, level);
    const auto chunkY = worldToChunkIndex(y, level);
    const TileKey key{chunkX, chunkY, level};

    if (const auto it = graph->tiles.find(key); it != graph->tiles.end())
    {
        return it->second.solution;
    }

    return std::nullopt;
}

int GraphRasterizer::intervalToState(const Interval &interval)
{
    if (interval.allTrue())
    {
        return 1;
    }

    if (interval.allFalse())
    {
        return 0;
    }

    return -1;
}

GraphRasterizer::SampleResult GraphRasterizer::samplePoint(const std::shared_ptr<Graph> &graph, const double x,
                                                           const double y, const int targetLevel)
{
    const auto exactChunkX = worldToChunkIndex(x, targetLevel);
    const auto exactChunkY = worldToChunkIndex(y, targetLevel);
    if (const auto exact = lookupAtLevel(graph, x, y, targetLevel))
    {
        return {intervalToState(*exact), LookupSource::Exact, targetLevel, exactChunkX, exactChunkY};
    }

    for (auto it = graph->activeLevels.lower_bound(targetLevel); it != graph->activeLevels.end(); ++it)
    {
        const auto level = *it;
        if (const auto parent = lookupAtLevel(graph, x, y, level))
        {
            return {intervalToState(*parent), LookupSource::Parent, level, worldToChunkIndex(x, level),
                    worldToChunkIndex(y, level)};
        }
    }

    const auto begin = graph->activeLevels.lower_bound(targetLevel);
    for (auto it = std::make_reverse_iterator(begin); it != graph->activeLevels.rend(); ++it)
    {
        const auto level = *it;
        if (const auto child = lookupAtLevel(graph, x, y, level))
        {
            return {intervalToState(*child), LookupSource::Child, level, worldToChunkIndex(x, level),
                    worldToChunkIndex(y, level)};
        }
    }

    return {-1, LookupSource::Missing, targetLevel, exactChunkX, exactChunkY};
}

RasterizedPlot GraphRasterizer::rasterize(const std::shared_ptr<Graph> &graph,
                                          const std::shared_ptr<Formula> &formula,
                                          const Interval &xRange,
                                          const Interval &yRange, const int windowWidth,
                                          const int windowHeight)
{
    if (!graph)
    {
        throw std::invalid_argument("Graph must not be null");
    }

    RasterizedPlot result;

    if (windowWidth <= 0 || windowHeight <= 0)
    {
        return result;
    }

    if (cachedFormula != formula.get())
    {
        mixedChunkTextureCache.clear();
        cachedFormula = formula.get();
    }

    const auto targetLevel = getTargetLevel(xRange, yRange, windowWidth, windowHeight);
    const auto [minChunkX, maxChunkX] = getChunkIndexBounds(xRange, targetLevel);
    const auto [minChunkY, maxChunkY] = getChunkIndexBounds(yRange, targetLevel);
    const auto targetChunkSize = chunkSizeForLevel(targetLevel);

    std::unordered_set<MixedTextureKey, MixedTextureKeyHash> emittedChunks;
    std::unordered_set<MixedTextureKey, MixedTextureKeyHash> mixedChunksInView;

    for (auto chunkY = minChunkY; chunkY <= maxChunkY; ++chunkY)
    {
        for (auto chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX)
        {
            const auto x = (static_cast<double>(chunkX) + 0.5) * targetChunkSize;
            const auto y = (static_cast<double>(chunkY) + 0.5) * targetChunkSize;
            const auto sample = samplePoint(graph, x, y, targetLevel);

            if (sample.source == LookupSource::Missing)
            {
                continue;
            }

            const auto chunkKey = MixedTextureKey{sample.chunkX, sample.chunkY, sample.levelUsed};
            const auto chunkSource = sample.source == LookupSource::Parent
                                         ? RasterChunkSource::Parent
                                         : (sample.source == LookupSource::Child
                                                ? RasterChunkSource::Child
                                                : RasterChunkSource::Exact);

            if (emittedChunks.insert(chunkKey).second)
            {
                const auto sampledXRange = chunkIndexToRange(sample.chunkX, sample.levelUsed);
                const auto sampledYRange = chunkIndexToRange(sample.chunkY, sample.levelUsed);
                result.chunks.push_back(
                    {sample.chunkX, sample.chunkY, sample.levelUsed, sampledXRange, sampledYRange, sample.state,
                     chunkSource});
            }

            if (sample.state < 0)
            {
                mixedChunksInView.insert(chunkKey);
            }
        }
    }

    if (mixedChunksInView.empty() || !formula)
    {
        return result;
    }

    constexpr auto textureSize = MIN_CHUNK_PIXELS;
    result.chunkTextures.reserve(mixedChunksInView.size());
    for (const auto &key : mixedChunksInView)
    {
        auto cacheIt = mixedChunkTextureCache.find(key);
        if (cacheIt == mixedChunkTextureCache.end())
        {
            const auto mixedXRange = chunkIndexToRange(key.chunkX, key.level);
            const auto mixedYRange = chunkIndexToRange(key.chunkY, key.level);
            std::vector<int> texturePixels;
            auto didRasterize = chunkRenderer
                                && chunkRenderer->rasterizeMixedChunkTexture(
                                    formula, mixedXRange, mixedYRange, textureSize, texturePixels);

            // If GPU renderer cannot serve this path, downgrade once to CPU and retry.
            if (!didRasterize && usingGpuChunkRenderer)
            {
                std::cout << "[GraphRasterizer] OpenCL backend cannot rasterize mixed chunk textures. "
                             "Switching to CPU backend."
                          << std::endl;
                chunkRenderer = std::make_unique<CpuChunkRenderer>();
                usingGpuChunkRenderer = false;
                didRasterize = chunkRenderer->rasterizeMixedChunkTexture(
                    formula, mixedXRange, mixedYRange, textureSize, texturePixels);
            }

            if (!didRasterize)
            {
                texturePixels.assign(static_cast<size_t>(textureSize) * static_cast<size_t>(textureSize), 0);
            }

            cacheIt = mixedChunkTextureCache.emplace(key, std::move(texturePixels)).first;
        }

        result.chunkTextures.push_back(
            {key.chunkX, key.chunkY, key.level, textureSize, textureSize, cacheIt->second});
    }

    return result;
}
