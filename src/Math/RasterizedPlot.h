//
// Created by Codex on 2/27/2026.
//

#ifndef RASTERIZEDPLOT_H
#define RASTERIZEDPLOT_H

#include <cstdint>
#include <optional>
#include <vector>

#include "Interval.h"

enum class RasterChunkSource
{
    Exact = 0,
    Parent = 1,
    Child = 2
};

struct RasterChunk
{
    int64_t chunkX;
    int64_t chunkY;
    int level;
    Interval xRange;
    Interval yRange;
    int state;
    RasterChunkSource source;
};

struct RasterChunkTexture
{
    int64_t chunkX;
    int64_t chunkY;
    int level;
    int width;
    int height;
    std::vector<int> pixels;
};

struct RasterContourSegment
{
    double x0;
    double y0;
    double x1;
    double y1;
};

struct RasterChunkContour
{
    int64_t chunkX;
    int64_t chunkY;
    int level;
    std::vector<RasterContourSegment> segments;
};

struct ChunkRenderData
{
    RasterChunk chunk;
    std::optional<RasterChunkTexture> region;
    std::optional<RasterChunkContour> contour;
};

struct RasterizedPlot
{
    std::vector<ChunkRenderData> chunkRenderData;
};

#endif //RASTERIZEDPLOT_H
