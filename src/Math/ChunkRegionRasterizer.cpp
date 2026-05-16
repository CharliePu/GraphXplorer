//
// Created by charl on 2/28/2026.
//

#include "ChunkRegionRasterizer.h"

#include <iostream>

#include "CpuChunkRenderer.h"
#include "OpenCLChunkRenderer.h"
#include "../Formula/Formula.h"
#include "../Graph/Graph.h"

ChunkRegionRasterizer::ChunkRegionRasterizer(): chunkRenderer{nullptr},
                                                usingGpuChunkRenderer{false},
                                                chunkRegionCache{}
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

void ChunkRegionRasterizer::clearCache()
{
    chunkRegionCache.clear();
}

std::optional<RasterChunkTexture> ChunkRegionRasterizer::rasterizeChunkRegion(const int64_t chunkX,
                                                                              const int64_t chunkY,
                                                                              const int level,
                                                                              const std::shared_ptr<Formula> &formula)
{
    if (!formula)
    {
        return std::nullopt;
    }

    const ChunkKey key{chunkX, chunkY, level};
    constexpr auto textureSize = MIN_CHUNK_PIXELS;

    auto cacheIt = chunkRegionCache.find(key);
    if (cacheIt == chunkRegionCache.end())
    {
        const auto xRange = chunkIndexToRange(key.chunkX, key.level);
        const auto yRange = chunkIndexToRange(key.chunkY, key.level);

        std::vector<int> texturePixels;
        auto didRasterize = chunkRenderer
                            && chunkRenderer->rasterizeMixedChunkTexture(
                                formula, xRange, yRange, textureSize, texturePixels);

        // If GPU renderer cannot serve this path, downgrade once to CPU and retry.
        if (!didRasterize && usingGpuChunkRenderer)
        {
            std::cout << "[GraphRasterizer] OpenCL backend cannot rasterize mixed chunk regions. "
                         "Switching to CPU backend."
                      << std::endl;
            chunkRenderer = std::make_unique<CpuChunkRenderer>();
            usingGpuChunkRenderer = false;
            didRasterize = chunkRenderer->rasterizeMixedChunkTexture(formula, xRange, yRange, textureSize,
                                                                     texturePixels);
        }

        if (!didRasterize)
        {
            texturePixels.assign(static_cast<size_t>(textureSize) * static_cast<size_t>(textureSize), 0);
        }

        cacheIt = chunkRegionCache.emplace(key, std::move(texturePixels)).first;
    }

    return RasterChunkTexture{
        key.chunkX,
        key.chunkY,
        key.level,
        textureSize,
        textureSize,
        cacheIt->second
    };
}
