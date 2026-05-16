#ifndef TILEMATH_H
#define TILEMATH_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

#include "../Math/Interval.h"
#include "../Util/Contracts.h"

namespace gx
{
inline constexpr int MinChunkPixels = 128;
inline constexpr int MinTileLevel = -30;
inline constexpr int MaxTileLevel = 30;

[[nodiscard]] inline int clampTileLevel(const int level)
{
    return std::clamp(level, MinTileLevel, MaxTileLevel);
}

[[nodiscard]] inline double tileSizeForLevel(const int level)
{
    return std::exp2(static_cast<double>(level));
}

[[nodiscard]] inline int64_t worldToTileIndex(const double value, const int level)
{
    return static_cast<int64_t>(std::floor(value / tileSizeForLevel(level)));
}

[[nodiscard]] inline Interval tileIndexToRange(const int64_t index, const int level)
{
    const auto size = tileSizeForLevel(level);
    const auto lower = static_cast<double>(index) * size;
    return {lower, lower + size};
}

[[nodiscard]] inline Rect tileBounds(const TileKey &key)
{
    const auto xRange = tileIndexToRange(key.x, key.level);
    const auto yRange = tileIndexToRange(key.y, key.level);
    return {xRange.lower, xRange.upper, yRange.lower, yRange.upper};
}

[[nodiscard]] inline int64_t floorDivByPow2(const int64_t value, const int shift)
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

[[nodiscard]] inline bool parentCoversChild(const TileKey &parent, const TileKey &child)
{
    if (parent.level <= child.level)
    {
        return false;
    }
    const auto levelDelta = parent.level - child.level;
    return floorDivByPow2(child.x, levelDelta) == parent.x
        && floorDivByPow2(child.y, levelDelta) == parent.y;
}

[[nodiscard]] inline int targetTileLevel(const Interval &xRange,
                                         const Interval &yRange,
                                         const int framebufferWidth,
                                         const int framebufferHeight)
{
    const auto minRangeSize = std::min(xRange.size(), yRange.size());
    const auto maxFramebufferSize = std::max(framebufferWidth, framebufferHeight);
    if (minRangeSize <= 0.0 || maxFramebufferSize <= 0)
    {
        return 0;
    }
    const auto rangePerPixel = minRangeSize / static_cast<double>(maxFramebufferSize);
    const auto rangePerTile = rangePerPixel * static_cast<double>(MinChunkPixels);
    return clampTileLevel(static_cast<int>(std::floor(std::log2(rangePerTile))));
}

[[nodiscard]] inline std::pair<int64_t, int64_t> tileIndexBounds(const Interval &range, const int level)
{
    if (range.upper <= range.lower)
    {
        const auto index = worldToTileIndex(range.lower, level);
        return {index, index};
    }
    const auto tileSize = tileSizeForLevel(level);
    const auto minIndex = worldToTileIndex(range.lower, level);
    const auto scaledUpper = range.upper / tileSize;
    const auto upperInclusiveScaled = std::nextafter(scaledUpper, -std::numeric_limits<double>::infinity());
    const auto maxIndex = static_cast<int64_t>(std::floor(upperInclusiveScaled));
    return {minIndex, maxIndex};
}
}

#endif // TILEMATH_H
