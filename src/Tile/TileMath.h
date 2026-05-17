#ifndef TILEMATH_H
#define TILEMATH_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include "../Math/Interval.h"
#include "../Util/Contracts.h"

namespace gx
{
inline constexpr int RasterTileScreenPixels = 256;
inline constexpr int RasterTexturePixels = RasterTileScreenPixels * 2;
inline constexpr int RootRefinementLevels = 2;
inline constexpr int LeafRefinementLevels = 0;
inline constexpr int DefaultRefinementDepth = 3;
inline constexpr int LowestFiniteTileLevel =
    std::numeric_limits<double>::min_exponent - std::numeric_limits<double>::digits;
inline constexpr int HighestFiniteTileLevel = std::numeric_limits<double>::max_exponent - 1;

[[nodiscard]] inline int clampFiniteTileLevel(const int level)
{
    return std::clamp(level, LowestFiniteTileLevel, HighestFiniteTileLevel);
}

[[nodiscard]] inline int floorLog2ToFiniteTileLevel(const double value)
{
    if (!std::isfinite(value) || value <= 0.0)
    {
        return 0;
    }

    const auto level = std::floor(std::log2(value));
    if (level <= static_cast<double>(LowestFiniteTileLevel))
    {
        return LowestFiniteTileLevel;
    }
    if (level >= static_cast<double>(HighestFiniteTileLevel))
    {
        return HighestFiniteTileLevel;
    }
    return static_cast<int>(level);
}

[[nodiscard]] inline double tileSizeForLevel(const int level)
{
    return std::exp2(static_cast<double>(level));
}

[[nodiscard]] inline int64_t worldToTileIndex(const double value, const int level)
{
    const auto scaled = std::floor(value / tileSizeForLevel(level));
    if (!std::isfinite(scaled))
    {
        return scaled < 0.0 ? std::numeric_limits<int64_t>::min() : std::numeric_limits<int64_t>::max();
    }
    if (scaled <= static_cast<double>(std::numeric_limits<int64_t>::min()))
    {
        return std::numeric_limits<int64_t>::min();
    }
    if (scaled >= static_cast<double>(std::numeric_limits<int64_t>::max()))
    {
        return std::numeric_limits<int64_t>::max();
    }
    return static_cast<int64_t>(scaled);
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

[[nodiscard]] inline bool intersects(const Rect &rect, const Interval &xRange, const Interval &yRange)
{
    return rect.xMax > xRange.lower
        && rect.xMin < xRange.upper
        && rect.yMax > yRange.lower
        && rect.yMin < yRange.upper;
}

[[nodiscard]] inline std::array<TileKey, 4> tileChildren(const TileKey &key)
{
    return {
        TileKey{key.x * 2, key.y * 2, key.level - 1},
        TileKey{key.x * 2 + 1, key.y * 2, key.level - 1},
        TileKey{key.x * 2, key.y * 2 + 1, key.level - 1},
        TileKey{key.x * 2 + 1, key.y * 2 + 1, key.level - 1}
    };
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

[[nodiscard]] inline TileKey tileParent(const TileKey &key)
{
    return {
        floorDivByPow2(key.x, 1),
        floorDivByPow2(key.y, 1),
        key.level + 1
    };
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
    const auto rangePerTile = rangePerPixel * static_cast<double>(RasterTileScreenPixels);
    return floorLog2ToFiniteTileLevel(rangePerTile);
}

[[nodiscard]] inline int rootTileLevel(const ViewportRequest &request)
{
    return clampFiniteTileLevel(targetTileLevel(
        request.xRange,
        request.yRange,
        request.framebufferWidth,
        request.framebufferHeight) + RootRefinementLevels);
}

[[nodiscard]] inline int leafTileLevel(const ViewportRequest &request)
{
    return clampFiniteTileLevel(targetTileLevel(
        request.xRange,
        request.yRange,
        request.framebufferWidth,
        request.framebufferHeight) - LeafRefinementLevels);
}

[[nodiscard]] inline std::pair<int64_t, int64_t> tileIndexBounds(const Interval &range, const int level)
{
    if (range.upper <= range.lower)
    {
        const auto index = worldToTileIndex(range.lower, level);
        return {index, index};
    }
    const auto minIndex = worldToTileIndex(range.lower, level);
    const auto upperInclusive = std::nextafter(range.upper, -std::numeric_limits<double>::infinity());
    const auto maxIndex = worldToTileIndex(upperInclusive, level);
    return {minIndex, maxIndex};
}

[[nodiscard]] inline std::optional<int64_t> tileCountForViewportAtLevel(
    const ViewportRequest &request,
    const int level,
    const int64_t maxCount)
{
    const auto countForAxis = [](const int64_t minIndex,
                                 const int64_t maxIndex) -> std::optional<unsigned long long>
    {
        if (maxIndex < minIndex)
        {
            return 0ull;
        }

        const auto count = static_cast<long double>(maxIndex)
            - static_cast<long double>(minIndex)
            + 1.0L;
        if (count > static_cast<long double>(std::numeric_limits<unsigned long long>::max()))
        {
            return std::nullopt;
        }
        return static_cast<unsigned long long>(count);
    };

    const auto [minX, maxX] = tileIndexBounds(request.xRange, level);
    const auto [minY, maxY] = tileIndexBounds(request.yRange, level);
    const auto width = countForAxis(minX, maxX);
    const auto height = countForAxis(minY, maxY);
    if (!width || !height)
    {
        return std::nullopt;
    }

    const auto limit = static_cast<unsigned long long>(std::max<int64_t>(0, maxCount));
    if (*width == 0 || *height == 0)
    {
        return 0;
    }
    if (*width > limit || *height > limit || *width > limit / *height)
    {
        return std::nullopt;
    }
    return static_cast<int64_t>(*width * *height);
}

[[nodiscard]] inline int seedTileLevelForViewport(const ViewportRequest &request, const int maxSeedCells = 4)
{
    if (!request.valid() || maxSeedCells <= 0)
    {
        return 0;
    }

    const auto maxSpan = std::max(request.xRange.size(), request.yRange.size());
    auto level = floorLog2ToFiniteTileLevel(
        maxSpan / std::max(1.0, std::sqrt(static_cast<double>(maxSeedCells))));

    while (level < HighestFiniteTileLevel)
    {
        const auto count = tileCountForViewportAtLevel(request, level, maxSeedCells);
        if (count && *count <= maxSeedCells)
        {
            break;
        }
        ++level;
    }

    while (level > LowestFiniteTileLevel)
    {
        const auto previous = level - 1;
        const auto previousCount = tileCountForViewportAtLevel(request, previous, maxSeedCells);
        if (!previousCount || *previousCount > maxSeedCells)
        {
            break;
        }
        level = previous;
    }

    return level;
}

[[nodiscard]] inline int leafTileLevelForSeed(const int seedLevel, const int refinementDepth)
{
    return clampFiniteTileLevel(seedLevel - std::max(0, refinementDepth));
}

[[nodiscard]] inline int leafTileLevel(const ViewportRequest &request,
                                       const int maxSeedCells,
                                       const int refinementDepth)
{
    return leafTileLevelForSeed(
        seedTileLevelForViewport(request, maxSeedCells),
        refinementDepth);
}
}

#endif // TILEMATH_H
