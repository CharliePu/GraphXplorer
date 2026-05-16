//
// Created by charl on 2/28/2026.
//

#ifndef CHUNKREGIONRASTERIZER_H
#define CHUNKREGIONRASTERIZER_H

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ChunkRenderer.h"
#include "RasterizedPlot.h"
#include "../Graph/Graph.h"

class Formula;

class ChunkRegionRasterizer
{
public:
    ChunkRegionRasterizer();

    void clearCache();

    std::optional<RasterChunkTexture> rasterizeChunkRegion(int64_t chunkX,
                                                           int64_t chunkY,
                                                           int level,
                                                           const std::shared_ptr<Formula> &formula);

private:
    struct ChunkKey
    {
        int64_t chunkX;
        int64_t chunkY;
        int level;

        bool operator==(const ChunkKey &other) const
        {
            return chunkX == other.chunkX && chunkY == other.chunkY && level == other.level;
        }
    };

    struct ChunkKeyHash
    {
        size_t operator()(const ChunkKey &key) const
        {
            return chunkKeyHash(key.chunkX, key.chunkY, key.level);
        }
    };

    std::unique_ptr<ChunkRenderer> chunkRenderer;
    bool usingGpuChunkRenderer;
    std::unordered_map<ChunkKey, std::vector<int>, ChunkKeyHash> chunkRegionCache;
};

#endif // CHUNKREGIONRASTERIZER_H
