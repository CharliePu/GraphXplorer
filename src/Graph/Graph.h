//
// Created by charl on 6/3/2024.
//

#ifndef GRAPH_H
#define GRAPH_H

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <unordered_map>

#include "../Math/Interval.h"

struct GraphNode
{
    GraphNode *parent;
    std::array<std::unique_ptr<GraphNode>, 4> children;
    Interval solution;
    Interval xRange;
    Interval yRange;
};

struct TileKey
{
    int64_t x;
    int64_t y;
    int level;

    bool operator==(const TileKey &other) const
    {
        return x == other.x && y == other.y && level == other.level;
    }
};

struct TileKeyHash
{
    size_t operator()(const TileKey &key) const
    {
        const auto h1 = std::hash<int64_t>{}(key.x);
        const auto h2 = std::hash<int64_t>{}(key.y);
        const auto h3 = std::hash<int>{}(key.level);

        size_t seed = h1;
        seed ^= h2 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        seed ^= h3 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct Tile
{
    Interval solution;
};

inline constexpr int MIN_CHUNK_PIXELS = 256;

inline double chunkSizeForLevel(const int level)
{
    return std::exp2(static_cast<double>(level));
}

inline int64_t worldToChunkIndex(const double value, const int level)
{
    const auto chunkSize = chunkSizeForLevel(level);
    return static_cast<int64_t>(std::floor(value / chunkSize));
}

inline Interval chunkIndexToRange(const int64_t chunkIndex, const int level)
{
    const auto chunkSize = chunkSizeForLevel(level);
    const auto lower = static_cast<double>(chunkIndex) * chunkSize;
    return {lower, lower + chunkSize};
}

struct Graph
{
    // Legacy root retained for compatibility with old code paths.
    std::unique_ptr<GraphNode> root;

    // New sparse multi-resolution cache.
    std::unordered_map<TileKey, Tile, TileKeyHash> tiles;
    std::set<int> activeLevels;
};

#endif //GRAPH_H
