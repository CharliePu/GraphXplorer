//
// Created by charl on 2/28/2026.
//

#include "ChunkContourRasterizer.h"

#include <algorithm>
#include <iostream>

#include "CpuChunkRenderer.h"
#include "OpenCLChunkRenderer.h"
#include "../Graph/Graph.h"

namespace
{
constexpr int kContourPixelsPerCell = 8;
constexpr int kMinContourCellsPerAxis = 4;
constexpr int kMaxContourCellsPerAxis = 64;

int contourCellsPerAxis()
{
    return std::clamp(MIN_CHUNK_PIXELS / kContourPixelsPerCell, kMinContourCellsPerAxis, kMaxContourCellsPerAxis);
}
}

ChunkContourRasterizer::ChunkContourRasterizer(): chunkRenderer{nullptr},
                                                  usingGpuChunkRenderer{false},
                                                  chunkContourCache{}
{
    auto gpuRenderer = std::make_unique<OpenCLChunkRenderer>();
    if (gpuRenderer->isAvailable())
    {
        chunkRenderer = std::move(gpuRenderer);
        usingGpuChunkRenderer = true;
        std::cout << "[GraphRasterizer] Contour renderer backend: OpenCL device" << std::endl;
        return;
    }

    chunkRenderer = std::make_unique<CpuChunkRenderer>();
    std::cout << "[GraphRasterizer] Contour renderer backend: CPU" << std::endl;
}

void ChunkContourRasterizer::clearCache()
{
    chunkContourCache.clear();
}

std::optional<RasterChunkContour> ChunkContourRasterizer::rasterizeChunkContour(const int64_t chunkX,
                                                                                 const int64_t chunkY,
                                                                                 const int level,
                                                                                 const RPN &residualRpn)
{
    const ChunkKey key{chunkX, chunkY, level};
    auto cacheIt = chunkContourCache.find(key);
    if (cacheIt == chunkContourCache.end())
    {
        const auto xRange = chunkIndexToRange(key.chunkX, key.level);
        const auto yRange = chunkIndexToRange(key.chunkY, key.level);
        const auto cellsPerAxis = contourCellsPerAxis();

        std::vector<RasterContourSegment> segments;
        auto didRasterize = chunkRenderer
            && chunkRenderer->rasterizeChunkContourSegments(
                residualRpn, xRange, yRange, cellsPerAxis, segments);

        // If GPU renderer failed or produced empty segments (float-precision
        // edge case), fall back to CPU which uses double precision.
        if (usingGpuChunkRenderer && (!didRasterize || segments.empty()))
        {
            if (!didRasterize)
            {
                std::cout << "[GraphRasterizer] OpenCL backend cannot rasterize chunk contours. "
                             "Switching to CPU backend."
                          << std::endl;
            }
            chunkRenderer = std::make_unique<CpuChunkRenderer>();
            usingGpuChunkRenderer = false;
            segments.clear();
            didRasterize = chunkRenderer->rasterizeChunkContourSegments(
                residualRpn, xRange, yRange, cellsPerAxis, segments);
        }

        cacheIt = chunkContourCache.emplace(key, std::move(segments)).first;
    }

    if (cacheIt->second.empty())
    {
        return std::nullopt;
    }

    return RasterChunkContour{
        key.chunkX,
        key.chunkY,
        key.level,
        cacheIt->second
    };
}
