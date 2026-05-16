//
// Created by charl on 2/28/2026.
//

#ifndef CHUNKCONTOURRASTERIZER_H
#define CHUNKCONTOURRASTERIZER_H

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ChunkRenderer.h"
#include "RasterizedPlot.h"
#include "../Graph/Graph.h"

class ChunkContourRasterizer
{
public:
    ChunkContourRasterizer();

    void clearCache();

    std::optional<RasterChunkContour> rasterizeChunkContour(int64_t chunkX,
                                                            int64_t chunkY,
                                                            int level,
                                                            const RPN &residualRpn);

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
    std::unordered_map<ChunkKey, std::vector<RasterContourSegment>, ChunkKeyHash> chunkContourCache;
};

#endif // CHUNKCONTOURRASTERIZER_H
