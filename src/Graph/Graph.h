#ifndef GRAPH_H
#define GRAPH_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <set>
#include <unordered_map>
#include <utility>

#include "../Math/Interval.h"

inline constexpr int MIN_CHUNK_PIXELS = 256;
inline constexpr int MIN_CHUNK_LEVEL = -30;
inline constexpr int MAX_CHUNK_LEVEL = 30;
inline constexpr double MIN_VIEWPORT_WORLD_SPAN = 0x1p-30;
inline constexpr double MAX_VIEWPORT_WORLD_SPAN = 0x1p30;

inline int clampChunkLevel(const int level)
{
    return std::clamp(level, MIN_CHUNK_LEVEL, MAX_CHUNK_LEVEL);
}

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

inline size_t chunkKeyHash(const int64_t x, const int64_t y, const int level)
{
    const auto h1 = std::hash<int64_t>{}(x);
    const auto h2 = std::hash<int64_t>{}(y);
    const auto h3 = std::hash<int>{}(level);

    size_t seed = h1;
    seed ^= h2 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    seed ^= h3 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

inline int targetLevel(const Interval &xRange, const Interval &yRange, const int windowWidth, const int windowHeight)
{
    const auto minRangeSize = std::min(xRange.size(), yRange.size());
    const auto maxWindowSize = std::max(windowWidth, windowHeight);

    if (minRangeSize <= 0.0 || maxWindowSize <= 0)
    {
        return 0;
    }

    const auto rangePerPixel = minRangeSize / static_cast<double>(maxWindowSize);
    const auto rangePerChunk = rangePerPixel * static_cast<double>(MIN_CHUNK_PIXELS);
    return clampChunkLevel(static_cast<int>(std::floor(std::log2(rangePerChunk))));
}

inline std::pair<int64_t, int64_t> chunkIndexBounds(const Interval &range, const int level)
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

inline int64_t floorDivByPow2(const int64_t value, const int shift)
{
    if (shift <= 0)
    {
        return value;
    }

    if (shift >= 62)
    {
        return value >= 0 ? 0 : -1;
    }

    const auto divisor = int64_t{1} << shift;
    if (value >= 0)
    {
        return value / divisor;
    }

    return -(((-value) + divisor - 1) / divisor);
}

inline bool parentCoversChild(const int64_t parentX, const int64_t parentY, const int parentLevel,
                              const int64_t childX, const int64_t childY, const int childLevel)
{
    if (parentLevel <= childLevel)
    {
        return false;
    }

    const auto levelDelta = parentLevel - childLevel;
    return floorDivByPow2(childX, levelDelta) == parentX
        && floorDivByPow2(childY, levelDelta) == parentY;
}

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
        return chunkKeyHash(key.x, key.y, key.level);
    }
};

struct Tile
{
    Interval solution;
};

struct Graph
{
    std::unordered_map<TileKey, Tile, TileKeyHash> tiles;
    std::set<int> activeLevels;
};

#endif //GRAPH_H
