//
// Created by Codex on 2/27/2026.
//

#ifndef RASTERIZEDPLOT_H
#define RASTERIZEDPLOT_H

#include <cstdint>
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

struct RasterizedPlot
{
    std::vector<RasterChunk> chunks;
    std::vector<RasterChunkTexture> chunkTextures;
};

#endif //RASTERIZEDPLOT_H
