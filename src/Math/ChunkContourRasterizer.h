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
            const auto h1 = std::hash<int64_t>{}(key.chunkX);
            const auto h2 = std::hash<int64_t>{}(key.chunkY);
            const auto h3 = std::hash<int>{}(key.level);

            size_t seed = h1;
            seed ^= h2 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            seed ^= h3 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    std::unique_ptr<ChunkRenderer> chunkRenderer;
    bool usingGpuChunkRenderer;
    std::unordered_map<ChunkKey, std::vector<RasterContourSegment>, ChunkKeyHash> chunkContourCache;
};

#endif // CHUNKCONTOURRASTERIZER_H
