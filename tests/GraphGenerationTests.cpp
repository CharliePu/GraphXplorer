//
// Created by Codex on 2/27/2026.
//

#include "catch.hpp"

#include <algorithm>
#include <cmath>

#include "../src/Formula/Formula.h"
#include "../src/Graph/Graph.h"
#include "../src/Math/GraphProcessor.h"
#include "../src/Math/GraphRasterizer.h"
#include "../src/Util/ThreadPool.h"

namespace
{
struct SelectionKey
{
    int64_t x;
    int64_t y;
    int level;

    bool operator==(const SelectionKey &other) const
    {
        return x == other.x && y == other.y && level == other.level;
    }
};

struct SelectionKeyHash
{
    size_t operator()(const SelectionKey &key) const
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

bool hasNonEmptyCoverageAt(const RasterizedPlot &plot, const double x, const double y)
{
    for (const auto &chunkRenderData : plot.chunkRenderData)
    {
        const auto &chunk = chunkRenderData.chunk;
        if (chunk.state == 0)
        {
            continue;
        }

        if (chunk.xRange.contains(x) && chunk.yRange.contains(y))
        {
            return true;
        }
    }

    return false;
}

int getTargetLevel(const Interval &xRange, const Interval &yRange, const int windowWidth, const int windowHeight)
{
    const auto minRangeSize = std::min(xRange.size(), yRange.size());
    const auto maxWindowSize = std::max(windowWidth, windowHeight);

    if (minRangeSize <= 0.0 || maxWindowSize <= 0)
    {
        return 0;
    }

    const auto rangePerPixel = minRangeSize / static_cast<double>(maxWindowSize);
    const auto rangePerChunk = rangePerPixel * static_cast<double>(MIN_CHUNK_PIXELS);
    return static_cast<int>(std::floor(std::log2(rangePerChunk)));
}

std::pair<int64_t, int64_t> getChunkIndexBounds(const Interval &range, const int level)
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

std::optional<SelectionKey> findBestChunkForTarget(
    const int64_t chunkX,
    const int64_t chunkY,
    const int targetLevel,
    const std::unordered_map<SelectionKey, RasterChunk, SelectionKeyHash> &chunksByKey,
    const std::unordered_set<SelectionKey, SelectionKeyHash> &texturedKeys,
    const std::set<int> &levels)
{
    const auto isRenderable = [&chunksByKey, &texturedKeys](const SelectionKey &key) -> bool
    {
        const auto it = chunksByKey.find(key);
        if (it == chunksByKey.end())
        {
            return false;
        }

        const auto state = it->second.state;
        if (state > 0)
        {
            return true;
        }

        if (state < 0)
        {
            return texturedKeys.contains(key);
        }

        return false;
    };

    const SelectionKey exactKey{chunkX, chunkY, targetLevel};
    if (isRenderable(exactKey))
    {
        return exactKey;
    }

    const auto chunkXRange = chunkIndexToRange(chunkX, targetLevel);
    const auto chunkYRange = chunkIndexToRange(chunkY, targetLevel);
    const auto centerX = (chunkXRange.lower + chunkXRange.upper) * 0.5;
    const auto centerY = (chunkYRange.lower + chunkYRange.upper) * 0.5;

    for (auto it = levels.lower_bound(targetLevel); it != levels.end(); ++it)
    {
        const auto level = *it;
        if (level == targetLevel)
        {
            continue;
        }

        const SelectionKey parentKey{
            worldToChunkIndex(centerX, level),
            worldToChunkIndex(centerY, level),
            level
        };
        if (isRenderable(parentKey))
        {
            return parentKey;
        }
    }

    return std::nullopt;
}

std::vector<SelectionKey> findCompleteRenderableChildrenForTarget(
    const int64_t chunkX,
    const int64_t chunkY,
    const int targetLevel,
    const std::unordered_map<SelectionKey, RasterChunk, SelectionKeyHash> &chunksByKey,
    const std::unordered_set<SelectionKey, SelectionKeyHash> &texturedKeys,
    const std::set<int> &levels)
{
    const auto isRenderable = [&chunksByKey, &texturedKeys](const SelectionKey &key) -> bool
    {
        const auto it = chunksByKey.find(key);
        if (it == chunksByKey.end())
        {
            return false;
        }

        const auto state = it->second.state;
        if (state > 0)
        {
            return true;
        }

        if (state < 0)
        {
            return texturedKeys.contains(key);
        }

        return false;
    };

    const auto targetXRange = chunkIndexToRange(chunkX, targetLevel);
    const auto targetYRange = chunkIndexToRange(chunkY, targetLevel);

    const auto begin = levels.lower_bound(targetLevel);
    for (auto it = std::make_reverse_iterator(begin); it != levels.rend(); ++it)
    {
        const auto level = *it;
        auto [minChildX, maxChildX] = getChunkIndexBounds(targetXRange, level);
        auto [minChildY, maxChildY] = getChunkIndexBounds(targetYRange, level);

        std::vector<SelectionKey> candidateKeys;
        candidateKeys.reserve(
            static_cast<size_t>(maxChildX - minChildX + 1) * static_cast<size_t>(maxChildY - minChildY + 1));

        auto complete = true;
        for (auto childY = minChildY; childY <= maxChildY && complete; ++childY)
        {
            for (auto childX = minChildX; childX <= maxChildX; ++childX)
            {
                const SelectionKey key{childX, childY, level};
                if (!isRenderable(key))
                {
                    complete = false;
                    break;
                }
                candidateKeys.push_back(key);
            }
        }

        if (complete && !candidateKeys.empty())
        {
            return candidateKeys;
        }
    }

    return {};
}

std::vector<SelectionKey> selectVisibleChunkKeysAtLevel(
    const RasterizedPlot &plot,
    const Interval &viewXRange,
    const Interval &viewYRange,
    const int windowWidth,
    const int windowHeight,
    const int targetLevel)
{
    std::unordered_map<SelectionKey, RasterChunk, SelectionKeyHash> chunksByKey;
    std::set<int> levels;
    std::unordered_set<SelectionKey, SelectionKeyHash> texturedKeys;
    for (const auto &chunkRenderData : plot.chunkRenderData)
    {
        const auto &chunk = chunkRenderData.chunk;
        const SelectionKey key{chunk.chunkX, chunk.chunkY, chunk.level};
        chunksByKey.insert_or_assign(key, chunk);
        levels.insert(chunk.level);

        if (chunkRenderData.region.has_value())
        {
            const auto &texture = *chunkRenderData.region;
            texturedKeys.insert({texture.chunkX, texture.chunkY, texture.level});
        }
    }

    if (chunksByKey.empty())
    {
        return {};
    }

    const auto [minChunkX, maxChunkX] = getChunkIndexBounds(viewXRange, targetLevel);
    const auto [minChunkY, maxChunkY] = getChunkIndexBounds(viewYRange, targetLevel);

    struct TargetCellKey
    {
        int64_t x;
        int64_t y;

        bool operator==(const TargetCellKey &other) const
        {
            return x == other.x && y == other.y;
        }
    };

    struct TargetCellKeyHash
    {
        size_t operator()(const TargetCellKey &key) const
        {
            const auto h1 = std::hash<int64_t>{}(key.x);
            const auto h2 = std::hash<int64_t>{}(key.y);
            size_t seed = h1;
            seed ^= h2 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    struct TargetCellRect
    {
        int64_t minX;
        int64_t maxX;
        int64_t minY;
        int64_t maxY;
    };

    const auto getTargetRectForKey =
        [&chunksByKey, targetLevel, minChunkX, maxChunkX, minChunkY, maxChunkY](const SelectionKey &key)
    -> std::optional<TargetCellRect>
    {
        const auto chunkIt = chunksByKey.find(key);
        if (chunkIt == chunksByKey.end())
        {
            return std::nullopt;
        }

        const auto &chunk = chunkIt->second;
        auto [coverMinX, coverMaxX] = getChunkIndexBounds(chunk.xRange, targetLevel);
        auto [coverMinY, coverMaxY] = getChunkIndexBounds(chunk.yRange, targetLevel);

        coverMinX = std::max(coverMinX, minChunkX);
        coverMaxX = std::min(coverMaxX, maxChunkX);
        coverMinY = std::max(coverMinY, minChunkY);
        coverMaxY = std::min(coverMaxY, maxChunkY);

        if (coverMinX > coverMaxX || coverMinY > coverMaxY)
        {
            return std::nullopt;
        }

        return TargetCellRect{coverMinX, coverMaxX, coverMinY, coverMaxY};
    };

    std::unordered_set<TargetCellKey, TargetCellKeyHash> coveredCells;
    coveredCells.reserve(
        static_cast<size_t>(maxChunkX - minChunkX + 1) * static_cast<size_t>(maxChunkY - minChunkY + 1));

    std::unordered_set<SelectionKey, SelectionKeyHash> chosenKeys;
    chosenKeys.reserve(
        static_cast<size_t>(maxChunkX - minChunkX + 1) * static_cast<size_t>(maxChunkY - minChunkY + 1));

    const auto markRectCovered = [&coveredCells](const TargetCellRect &rect)
    {
        for (auto y = rect.minY; y <= rect.maxY; ++y)
        {
            for (auto x = rect.minX; x <= rect.maxX; ++x)
            {
                coveredCells.insert({x, y});
            }
        }
    };

    const auto floorDivByPow2 = [](const int64_t value, const int shift) -> int64_t
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
    };

    const auto parentCoversChild = [floorDivByPow2](const SelectionKey &parent, const SelectionKey &child) -> bool
    {
        if (parent.level <= child.level)
        {
            return false;
        }

        const auto levelDelta = parent.level - child.level;
        const auto projectedX = floorDivByPow2(child.x, levelDelta);
        const auto projectedY = floorDivByPow2(child.y, levelDelta);
        return projectedX == parent.x && projectedY == parent.y;
    };

    const auto evictCoveredFiner = [&chosenKeys, parentCoversChild](const SelectionKey &coarseKey)
    {
        for (auto it = chosenKeys.begin(); it != chosenKeys.end();)
        {
            if (!parentCoversChild(coarseKey, *it))
            {
                ++it;
                continue;
            }

            it = chosenKeys.erase(it);
        }
    };

    for (auto chunkY = minChunkY; chunkY <= maxChunkY; ++chunkY)
    {
        for (auto chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX)
        {
            const TargetCellKey cell{chunkX, chunkY};
            if (coveredCells.contains(cell))
            {
                continue;
            }

            if (const auto chosen = findBestChunkForTarget(
                chunkX, chunkY, targetLevel, chunksByKey, texturedKeys, levels))
            {
                if (chosen->level > targetLevel)
                {
                    evictCoveredFiner(*chosen);
                }

                chosenKeys.insert(*chosen);
                if (const auto rect = getTargetRectForKey(*chosen))
                {
                    markRectCovered(*rect);
                }
                else
                {
                    coveredCells.insert(cell);
                }
                continue;
            }

            const auto childKeys = findCompleteRenderableChildrenForTarget(
                chunkX, chunkY, targetLevel, chunksByKey, texturedKeys, levels);
            if (!childKeys.empty())
            {
                for (const auto &childKey : childKeys)
                {
                    chosenKeys.insert(childKey);
                }
                coveredCells.insert(cell);
            }
        }
    }

    std::vector<SelectionKey> keys;
    keys.reserve(chosenKeys.size());
    for (const auto &key : chosenKeys)
    {
        keys.push_back(key);
    }

    std::ranges::sort(keys, [](const SelectionKey &lhs, const SelectionKey &rhs)
    {
        if (lhs.level != rhs.level)
        {
            return lhs.level > rhs.level;
        }

        return std::tie(lhs.y, lhs.x) < std::tie(rhs.y, rhs.x);
    });

    std::vector<SelectionKey> prunedKeys;
    prunedKeys.reserve(keys.size());
    for (const auto &candidate : keys)
    {
        auto coveredByParent = false;
        for (const auto &selected : prunedKeys)
        {
            if (parentCoversChild(selected, candidate))
            {
                coveredByParent = true;
                break;
            }
        }

        if (!coveredByParent)
        {
            prunedKeys.push_back(candidate);
        }
    }

    return prunedKeys;
}

std::vector<SelectionKey> selectVisibleChunkKeys(
    const RasterizedPlot &plot,
    const Interval &viewXRange,
    const Interval &viewYRange,
    const int windowWidth,
    const int windowHeight)
{
    std::set<int> levels;
    for (const auto &chunkRenderData : plot.chunkRenderData)
    {
        const auto &chunk = chunkRenderData.chunk;
        levels.insert(chunk.level);
    }

    if (levels.empty())
    {
        return {};
    }

    const auto desiredTargetLevel = getTargetLevel(viewXRange, viewYRange, windowWidth, windowHeight);

    std::vector<int> candidateLevels;
    candidateLevels.reserve(levels.size() + 1);
    candidateLevels.push_back(desiredTargetLevel);

    for (const auto level : levels)
    {
        if (level != desiredTargetLevel)
        {
            candidateLevels.push_back(level);
        }
    }

    std::ranges::sort(candidateLevels, [desiredTargetLevel](int a, int b)
    {
        return std::abs(a - desiredTargetLevel) < std::abs(b - desiredTargetLevel);
    });

    for (const auto candidateLevel : candidateLevels)
    {
        auto keys = selectVisibleChunkKeysAtLevel(plot, viewXRange, viewYRange, windowWidth, windowHeight,
                                                  candidateLevel);
        if (!keys.empty())
        {
            return keys;
        }
    }

    return {};
}

bool hasNonEmptyCoverageAtFromSelection(
    const std::vector<SelectionKey> &selectedKeys,
    const std::unordered_map<SelectionKey, RasterChunk, SelectionKeyHash> &chunksByKey,
    const double x,
    const double y)
{
    for (const auto &key : selectedKeys)
    {
        const auto chunkIt = chunksByKey.find(key);
        if (chunkIt == chunksByKey.end())
        {
            continue;
        }

        const auto &chunk = chunkIt->second;
        if (chunk.state == 0)
        {
            continue;
        }

        if (chunk.xRange.contains(x) && chunk.yRange.contains(y))
        {
            return true;
        }
    }

    return false;
}

void validateMixedChunkTexturesAgainstXLeqY(const RasterizedPlot &plot)
{
    std::unordered_map<SelectionKey, RasterChunk, SelectionKeyHash> chunksByKey;
    for (const auto &chunkRenderData : plot.chunkRenderData)
    {
        const auto &chunk = chunkRenderData.chunk;
        chunksByKey.insert_or_assign({chunk.chunkX, chunk.chunkY, chunk.level}, chunk);
    }

    for (const auto &chunkRenderData : plot.chunkRenderData)
    {
        if (!chunkRenderData.region.has_value())
        {
            continue;
        }

        const auto &texture = *chunkRenderData.region;
        const SelectionKey key{texture.chunkX, texture.chunkY, texture.level};
        const auto chunkIt = chunksByKey.find(key);
        REQUIRE(chunkIt != chunksByKey.end());
        REQUIRE(chunkIt->second.state < 0);
        REQUIRE(texture.width > 0);
        REQUIRE(texture.height > 0);
        REQUIRE(texture.pixels.size() == static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height));

        const auto deltaX = chunkIt->second.xRange.size() / static_cast<double>(texture.width);
        const auto deltaY = chunkIt->second.yRange.size() / static_cast<double>(texture.height);

        const auto stepX = std::max(1, texture.width / 32);
        const auto stepY = std::max(1, texture.height / 32);

        for (auto py = 0; py < texture.height; py += stepY)
        {
            const auto sampleY = chunkIt->second.yRange.lower + (static_cast<double>(py) + 0.5) * deltaY;
            for (auto px = 0; px < texture.width; px += stepX)
            {
                const auto sampleX = chunkIt->second.xRange.lower + (static_cast<double>(px) + 0.5) * deltaX;
                const auto idx = static_cast<size_t>(py) * static_cast<size_t>(texture.width)
                                 + static_cast<size_t>(px);
                const auto expected = sampleX <= sampleY ? 1 : 0;
                CHECK(texture.pixels[idx] == expected);
            }
        }
    }
}

std::optional<bool> sampleSelectionValueAt(
    const double x,
    const double y,
    const std::vector<SelectionKey> &selectedKeys,
    const std::unordered_map<SelectionKey, RasterChunk, SelectionKeyHash> &chunksByKey,
    const std::unordered_map<SelectionKey, RasterChunkTexture, SelectionKeyHash> &texturesByKey)
{
    for (const auto &key : selectedKeys)
    {
        const auto chunkIt = chunksByKey.find(key);
        if (chunkIt == chunksByKey.end())
        {
            continue;
        }

        const auto &chunk = chunkIt->second;
        if (!chunk.xRange.contains(x) || !chunk.yRange.contains(y))
        {
            continue;
        }

        if (chunk.state >= 0)
        {
            return chunk.state > 0;
        }

        const auto textureIt = texturesByKey.find(key);
        if (textureIt == texturesByKey.end())
        {
            return std::nullopt;
        }

        const auto &texture = textureIt->second;
        if (texture.width <= 0 || texture.height <= 0 || texture.pixels.empty())
        {
            return std::nullopt;
        }

        const auto u = std::clamp((x - chunk.xRange.lower) / chunk.xRange.size(), 0.0, 0.999999999);
        const auto v = std::clamp((y - chunk.yRange.lower) / chunk.yRange.size(), 0.0, 0.999999999);
        const auto px = static_cast<int>(u * static_cast<double>(texture.width));
        const auto py = static_cast<int>(v * static_cast<double>(texture.height));
        const auto idx = static_cast<size_t>(py) * static_cast<size_t>(texture.width) + static_cast<size_t>(px);
        if (idx >= texture.pixels.size())
        {
            return std::nullopt;
        }

        return texture.pixels[idx] > 0;
    }

    return false;
}
}

TEST_CASE("Startup circle rasterization covers all quadrants", "[GraphGeneration][Startup]")
{
    const auto graph = std::make_shared<Graph>();
    const auto formula = std::make_shared<Formula>("x^2+y^2<4^2");
    const auto pool = std::make_shared<ThreadPool>(1);

    GraphProcessor processor(nullptr, pool);
    GraphRasterizer rasterizer(nullptr, pool);

    const Interval xRange{-20.0, 20.0};
    const Interval yRange{-20.0, 20.0};
    constexpr auto windowWidth = 800;
    constexpr auto windowHeight = 800;

    processor.process(graph, formula, xRange, yRange, windowWidth, windowHeight);
    const auto plot = rasterizer.rasterize(graph, formula, xRange, yRange, windowWidth, windowHeight);

    REQUIRE(hasNonEmptyCoverageAt(plot, -2.0, -2.0));
    REQUIRE(hasNonEmptyCoverageAt(plot, 2.0, -2.0));
    REQUIRE(hasNonEmptyCoverageAt(plot, -2.0, 2.0));
    REQUIRE(hasNonEmptyCoverageAt(plot, 2.0, 2.0));
}

TEST_CASE("Current visible-key selection keeps non-empty coverage in all quadrants", "[GraphGeneration][Selection]")
{
    const auto graph = std::make_shared<Graph>();
    const auto formula = std::make_shared<Formula>("x^2+y^2<4^2");
    const auto pool = std::make_shared<ThreadPool>(1);

    GraphProcessor processor(nullptr, pool);
    GraphRasterizer rasterizer(nullptr, pool);

    const Interval xRange{-20.0, 20.0};
    const Interval yRange{-20.0, 20.0};
    constexpr auto windowWidth = 800;
    constexpr auto windowHeight = 800;

    processor.process(graph, formula, xRange, yRange, windowWidth, windowHeight);
    const auto plot = rasterizer.rasterize(graph, formula, xRange, yRange, windowWidth, windowHeight);

    std::unordered_map<SelectionKey, RasterChunk, SelectionKeyHash> chunksByKey;
    for (const auto &chunkRenderData : plot.chunkRenderData)
    {
        const auto &chunk = chunkRenderData.chunk;
        chunksByKey.insert_or_assign({chunk.chunkX, chunk.chunkY, chunk.level}, chunk);
    }

    const auto selected = selectVisibleChunkKeys(plot, xRange, yRange, windowWidth, windowHeight);
    REQUIRE(hasNonEmptyCoverageAtFromSelection(selected, chunksByKey, -2.0, -2.0));
    REQUIRE(hasNonEmptyCoverageAtFromSelection(selected, chunksByKey, 2.0, -2.0));
    REQUIRE(hasNonEmptyCoverageAtFromSelection(selected, chunksByKey, -2.0, 2.0));
    REQUIRE(hasNonEmptyCoverageAtFromSelection(selected, chunksByKey, 2.0, 2.0));
}

TEST_CASE("Visible-key selection includes empty chunks when they are selected", "[GraphGeneration][Selection]")
{
    const auto graph = std::make_shared<Graph>();
    const auto formula = std::make_shared<Formula>("x^2+y^2<4^2");
    const auto pool = std::make_shared<ThreadPool>(1);

    GraphProcessor processor(nullptr, pool);
    GraphRasterizer rasterizer(nullptr, pool);

    const Interval xRange{-20.0, 20.0};
    const Interval yRange{-20.0, 20.0};
    constexpr auto windowWidth = 800;
    constexpr auto windowHeight = 800;

    processor.process(graph, formula, xRange, yRange, windowWidth, windowHeight);
    const auto plot = rasterizer.rasterize(graph, formula, xRange, yRange, windowWidth, windowHeight);

    std::unordered_map<SelectionKey, RasterChunk, SelectionKeyHash> chunksByKey;
    for (const auto &chunkRenderData : plot.chunkRenderData)
    {
        const auto &chunk = chunkRenderData.chunk;
        chunksByKey.insert_or_assign({chunk.chunkX, chunk.chunkY, chunk.level}, chunk);
    }

    const auto selected = selectVisibleChunkKeys(plot, xRange, yRange, windowWidth, windowHeight);
    const auto hasEmptySelected = std::ranges::any_of(selected, [&chunksByKey](const SelectionKey &key)
    {
        const auto it = chunksByKey.find(key);
        return it != chunksByKey.end() && it->second.state == 0;
    });

    REQUIRE_FALSE(hasEmptySelected);
}

TEST_CASE("Visible-key selection avoids overlapping parent and child selections", "[GraphGeneration][Selection]")
{
    const auto graph = std::make_shared<Graph>();
    const auto formula = std::make_shared<Formula>("x + y > 0");
    const auto pool = std::make_shared<ThreadPool>(1);

    GraphProcessor processor(nullptr, pool);
    GraphRasterizer rasterizer(nullptr, pool);

    const Interval xRange{-20.0, 20.0};
    const Interval yRange{-20.0, 20.0};
    constexpr auto windowWidth = 800;
    constexpr auto windowHeight = 800;

    processor.process(graph, formula, xRange, yRange, windowWidth, windowHeight);
    const auto plot = rasterizer.rasterize(graph, formula, xRange, yRange, windowWidth, windowHeight);
    const auto selected = selectVisibleChunkKeys(plot, xRange, yRange, windowWidth, windowHeight);

    std::unordered_map<SelectionKey, RasterChunk, SelectionKeyHash> chunksByKey;
    for (const auto &chunkRenderData : plot.chunkRenderData)
    {
        const auto &chunk = chunkRenderData.chunk;
        chunksByKey.insert_or_assign({chunk.chunkX, chunk.chunkY, chunk.level}, chunk);
    }

    const auto overlaps = [](const Interval &lhs, const Interval &rhs) -> bool
    {
        return lhs.lower < rhs.upper && rhs.lower < lhs.upper;
    };

    for (size_t i = 0; i < selected.size(); ++i)
    {
        for (size_t j = i + 1; j < selected.size(); ++j)
        {
            const auto &a = selected[i];
            const auto &b = selected[j];
            const auto aIt = chunksByKey.find(a);
            const auto bIt = chunksByKey.find(b);
            REQUIRE(aIt != chunksByKey.end());
            REQUIRE(bIt != chunksByKey.end());

            const auto overlap = overlaps(aIt->second.xRange, bIt->second.xRange)
                                 && overlaps(aIt->second.yRange, bIt->second.yRange);
            if (!overlap)
            {
                continue;
            }

            REQUIRE(a.level == b.level);
        }
    }
}

TEST_CASE("Selection falls back to finer cached levels when coarse target has no matches", "[GraphGeneration][Selection]")
{
    const Interval xRange{-20.0, 20.0};
    const Interval yRange{-20.0, 20.0};
    constexpr auto windowWidth = 800;
    constexpr auto windowHeight = 800;

    const auto desiredTargetLevel = getTargetLevel(xRange, yRange, windowWidth, windowHeight);
    REQUIRE(desiredTargetLevel == 3);

    RasterizedPlot plot;
    {
        constexpr int fineLevel = 1;
        const auto fineXRange = chunkIndexToRange(0, fineLevel);
        const auto fineYRange = chunkIndexToRange(0, fineLevel);
        plot.chunkRenderData.push_back({
            {0, 0, fineLevel, fineXRange, fineYRange, 1, RasterChunkSource::Exact},
            std::nullopt,
            std::nullopt
        });
    }

    const auto coarseOnly = selectVisibleChunkKeysAtLevel(
        plot, xRange, yRange, windowWidth, windowHeight, desiredTargetLevel);
    REQUIRE(coarseOnly.empty());

    const auto adaptiveSelected = selectVisibleChunkKeys(plot, xRange, yRange, windowWidth, windowHeight);
    REQUIRE_FALSE(adaptiveSelected.empty());
    REQUIRE(std::ranges::any_of(adaptiveSelected, [](const SelectionKey &key)
    {
        return key.x == 0 && key.y == 0 && key.level == 1;
    }));
}

TEST_CASE("x<=y mixed textures stay correct across pan updates", "[GraphGeneration][Raster]")
{
    const auto graph = std::make_shared<Graph>();
    const auto formula = std::make_shared<Formula>("x<=y");
    const auto pool = std::make_shared<ThreadPool>(1);

    GraphProcessor processor(nullptr, pool);
    GraphRasterizer rasterizer(nullptr, pool);

    constexpr auto windowWidth = 800;
    constexpr auto windowHeight = 800;

    const std::array xRanges{
        Interval{-20.0, 20.0},
        Interval{-12.0, 28.0},
        Interval{-4.0, 36.0}
    };
    const std::array yRanges{
        Interval{-20.0, 20.0},
        Interval{-16.0, 24.0},
        Interval{-8.0, 32.0}
    };

    for (size_t i = 0; i < xRanges.size(); ++i)
    {
        processor.process(graph, formula, xRanges[i], yRanges[i], windowWidth, windowHeight);
        const auto plot = rasterizer.rasterize(graph, formula, xRanges[i], yRanges[i], windowWidth, windowHeight);
        validateMixedChunkTexturesAgainstXLeqY(plot);
    }
}

TEST_CASE("x<=y selected chunks match analytic predicate away from boundary", "[GraphGeneration][Selection]")
{
    const auto graph = std::make_shared<Graph>();
    const auto formula = std::make_shared<Formula>("x<=y");
    const auto pool = std::make_shared<ThreadPool>(1);

    GraphProcessor processor(nullptr, pool);
    GraphRasterizer rasterizer(nullptr, pool);

    constexpr auto windowWidth = 800;
    constexpr auto windowHeight = 800;

    const std::array xRanges{
        Interval{-20.0, 20.0},
        Interval{-12.0, 28.0},
        Interval{-4.0, 36.0}
    };
    const std::array yRanges{
        Interval{-20.0, 20.0},
        Interval{-16.0, 24.0},
        Interval{-8.0, 32.0}
    };

    for (size_t i = 0; i < xRanges.size(); ++i)
    {
        processor.process(graph, formula, xRanges[i], yRanges[i], windowWidth, windowHeight);
        const auto plot = rasterizer.rasterize(graph, formula, xRanges[i], yRanges[i], windowWidth, windowHeight);
        const auto selected = selectVisibleChunkKeys(plot, xRanges[i], yRanges[i], windowWidth, windowHeight);

        std::unordered_map<SelectionKey, RasterChunk, SelectionKeyHash> chunksByKey;
        for (const auto &chunkRenderData : plot.chunkRenderData)
        {
            const auto &chunk = chunkRenderData.chunk;
            chunksByKey.insert_or_assign({chunk.chunkX, chunk.chunkY, chunk.level}, chunk);
        }

        std::unordered_map<SelectionKey, RasterChunkTexture, SelectionKeyHash> texturesByKey;
        for (const auto &chunkRenderData : plot.chunkRenderData)
        {
            if (!chunkRenderData.region.has_value())
            {
                continue;
            }

            const auto &texture = *chunkRenderData.region;
            texturesByKey.insert_or_assign({texture.chunkX, texture.chunkY, texture.level}, texture);
        }

        constexpr auto samplesPerAxis = 96;
        const auto stepX = xRanges[i].size() / static_cast<double>(samplesPerAxis);
        const auto stepY = yRanges[i].size() / static_cast<double>(samplesPerAxis);
        constexpr auto boundaryEpsilon = 0.25;

        for (auto sy = 0; sy < samplesPerAxis; ++sy)
        {
            const auto y = yRanges[i].lower + (static_cast<double>(sy) + 0.5) * stepY;
            for (auto sx = 0; sx < samplesPerAxis; ++sx)
            {
                const auto x = xRanges[i].lower + (static_cast<double>(sx) + 0.5) * stepX;
                if (std::abs(x - y) <= boundaryEpsilon)
                {
                    continue;
                }

                const auto rendered = sampleSelectionValueAt(x, y, selected, chunksByKey, texturesByKey);
                REQUIRE(rendered.has_value());
                const auto expected = x <= y;
                CHECK(rendered.value() == expected);
            }
        }
    }
}

TEST_CASE("x<=y selected mixed chunks always carry contour segments", "[GraphGeneration][Contour][Selection]")
{
    const auto graph = std::make_shared<Graph>();
    const auto formula = std::make_shared<Formula>("x<=y");
    const auto pool = std::make_shared<ThreadPool>(1);

    GraphProcessor processor(nullptr, pool);
    GraphRasterizer rasterizer(nullptr, pool);

    const Interval xRange{-20.0, 20.0};
    const Interval yRange{-20.0, 20.0};
    constexpr auto windowWidth = 800;
    constexpr auto windowHeight = 800;

    processor.process(graph, formula, xRange, yRange, windowWidth, windowHeight);
    const auto plot = rasterizer.rasterize(graph, formula, xRange, yRange, windowWidth, windowHeight);
    const auto selected = selectVisibleChunkKeys(plot, xRange, yRange, windowWidth, windowHeight);

    std::unordered_map<SelectionKey, const ChunkRenderData *, SelectionKeyHash> byKey;
    byKey.reserve(plot.chunkRenderData.size());
    for (const auto &chunkRenderData : plot.chunkRenderData)
    {
        const auto &chunk = chunkRenderData.chunk;
        byKey.insert_or_assign({chunk.chunkX, chunk.chunkY, chunk.level}, &chunkRenderData);
    }

    auto checkedMixed = 0;
    for (const auto &key : selected)
    {
        const auto it = byKey.find(key);
        REQUIRE(it != byKey.end());
        const auto *data = it->second;
        REQUIRE(data != nullptr);

        if (data->chunk.state >= 0)
        {
            continue;
        }

        ++checkedMixed;

        const auto overlapLower = std::max(data->chunk.xRange.lower, data->chunk.yRange.lower);
        const auto overlapUpper = std::min(data->chunk.xRange.upper, data->chunk.yRange.upper);
        if (overlapLower >= overlapUpper)
        {
            continue;
        }

        REQUIRE(data->contour.has_value());
        REQUIRE_FALSE(data->contour->segments.empty());
    }

    REQUIRE(checkedMixed > 0);
}

TEST_CASE("x<=y contour coordinates are correct for chunk (0,0) at level 0", "[GraphGeneration][Contour][Coordinates]")
{
    const auto graph = std::make_shared<Graph>();
    const auto formula = std::make_shared<Formula>("x<=y");
    const auto pool = std::make_shared<ThreadPool>(1);

    GraphProcessor processor(nullptr, pool);
    GraphRasterizer rasterizer(nullptr, pool);

    // This viewport/window pairing yields target level 0 and isolates chunk (0,0).
    const Interval xRange{0.0, 1.0};
    const Interval yRange{0.0, 1.0};
    constexpr auto windowWidth = 256;
    constexpr auto windowHeight = 256;

    processor.process(graph, formula, xRange, yRange, windowWidth, windowHeight);
    const auto plot = rasterizer.rasterize(graph, formula, xRange, yRange, windowWidth, windowHeight);

    const auto it = std::ranges::find_if(plot.chunkRenderData, [](const ChunkRenderData &data)
    {
        return data.chunk.chunkX == 0 && data.chunk.chunkY == 0 && data.chunk.level == 0;
    });
    REQUIRE(it != plot.chunkRenderData.end());

    const auto &chunkData = *it;
    REQUIRE(chunkData.chunk.state < 0);
    REQUIRE(chunkData.contour.has_value());
    REQUIRE_FALSE(chunkData.contour->segments.empty());

    constexpr auto boundsEpsilon = 1e-5;
    constexpr auto diagonalEpsilon = 1e-3;

    auto minCoord = std::numeric_limits<double>::infinity();
    auto maxCoord = -std::numeric_limits<double>::infinity();

    for (const auto &segment : chunkData.contour->segments)
    {
        REQUIRE(std::isfinite(segment.x0));
        REQUIRE(std::isfinite(segment.y0));
        REQUIRE(std::isfinite(segment.x1));
        REQUIRE(std::isfinite(segment.y1));

        CHECK(segment.x0 >= 0.0 - boundsEpsilon);
        CHECK(segment.x0 <= 1.0 + boundsEpsilon);
        CHECK(segment.y0 >= 0.0 - boundsEpsilon);
        CHECK(segment.y0 <= 1.0 + boundsEpsilon);
        CHECK(segment.x1 >= 0.0 - boundsEpsilon);
        CHECK(segment.x1 <= 1.0 + boundsEpsilon);
        CHECK(segment.y1 >= 0.0 - boundsEpsilon);
        CHECK(segment.y1 <= 1.0 + boundsEpsilon);

        CHECK(std::abs(segment.y0 - segment.x0) <= diagonalEpsilon);
        CHECK(std::abs(segment.y1 - segment.x1) <= diagonalEpsilon);

        minCoord = std::min({minCoord, segment.x0, segment.y0, segment.x1, segment.y1});
        maxCoord = std::max({maxCoord, segment.x0, segment.y0, segment.x1, segment.y1});
    }

    CHECK(minCoord <= 0.0 + diagonalEpsilon);
    CHECK(maxCoord >= 1.0 - diagonalEpsilon);
}
