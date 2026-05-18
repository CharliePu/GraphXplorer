#include "VisualCoverBuilder.h"

#include <algorithm>
#include <array>
#include <optional>
#include <tuple>
#include <utility>

#include "../Tile/TileMath.h"

namespace gx
{
namespace
{
[[nodiscard]] Rect intersectRect(const Rect &rect, const Interval &xRange, const Interval &yRange)
{
    return {
        std::max(rect.xMin, xRange.lower),
        std::min(rect.xMax, xRange.upper),
        std::max(rect.yMin, yRange.lower),
        std::min(rect.yMax, yRange.upper)
    };
}

[[nodiscard]] bool validRect(const Rect &rect)
{
    return rect.xMin < rect.xMax && rect.yMin < rect.yMax;
}

[[nodiscard]] std::array<float, 4> uvRectForTileSource(const Rect &destination, const Rect &source)
{
    const auto sourceWidth = source.xMax - source.xMin;
    const auto sourceHeight = source.yMax - source.yMin;
    if (sourceWidth <= 0.0 || sourceHeight <= 0.0)
    {
        return {0.0f, 0.0f, 1.0f, 1.0f};
    }

    return {
        static_cast<float>((destination.xMin - source.xMin) / sourceWidth),
        static_cast<float>((destination.yMin - source.yMin) / sourceHeight),
        static_cast<float>((destination.xMax - source.xMin) / sourceWidth),
        static_cast<float>((destination.yMax - source.yMin) / sourceHeight)
    };
}

[[nodiscard]] bool presentableCommittedTile(const DisplayTile &tile)
{
    switch (tile.visualState)
    {
    case TileVisualState::UniformTrue:
    case TileVisualState::UniformFalse:
        return true;
    case TileVisualState::MixedRegion:
        return tile.cpuRegion.has_value() && tile.gpuSlice.textureId != 0;
    default:
        return false;
    }
}

[[nodiscard]] bool coversDisplayKey(const TileKey &candidate, const TileKey &key)
{
    return candidate == key || parentCoversChild(candidate, key);
}

[[nodiscard]] bool tileIntersectsViewport(const TileKey &key, const ViewportRequest &request)
{
    return intersects(tileBounds(key), request.xRange, request.yRange);
}
}

VisualFrame VisualCoverBuilder::build(const ViewportRequest &request,
                                      const TileCache &tileCache,
                                      const CommittedVisualFrame *previous,
                                      const int maxSeedCells,
                                      const int refinementDepth,
                                      RegionPresentablePredicate regionPresentable) const
{
    VisualFrame empty;
    if (!request.valid() || maxSeedCells <= 0)
    {
        return empty;
    }

    if (previous && previous->semantics != request.formula.semanticsHash)
    {
        previous = nullptr;
    }

    const auto seedLevel = seedTileLevelForViewport(request, maxSeedCells);
    const auto [minX, maxX] = tileIndexBounds(request.xRange, seedLevel);
    const auto [minY, maxY] = tileIndexBounds(request.yRange, seedLevel);
    const auto width = maxX - minX + 1;
    const auto height = maxY - minY + 1;
    if (width <= 0 || height <= 0 || width * height > maxSeedCells)
    {
        return empty;
    }

    BuildState state;
    state.regionPresentable = std::move(regionPresentable);
    state.previous = previous;
    state.frame.tiles.reserve(static_cast<size_t>(width * height));
    const auto leafLevel = leafTileLevelForSeed(seedLevel, refinementDepth);
    for (auto y = minY; y <= maxY; ++y)
    {
        for (auto x = minX; x <= maxX; ++x)
        {
            visit(request,
                  tileCache,
                  previous,
                  TileKey{x, y, seedLevel},
                  leafLevel,
                  state);
        }
    }

    std::ranges::sort(state.frame.tiles, [](const DisplayTile &lhs, const DisplayTile &rhs)
    {
        if (lhs.worldBounds.yMin != rhs.worldBounds.yMin)
        {
            return lhs.worldBounds.yMin < rhs.worldBounds.yMin;
        }
        if (lhs.worldBounds.xMin != rhs.worldBounds.xMin)
        {
            return lhs.worldBounds.xMin < rhs.worldBounds.xMin;
        }
        return std::tie(lhs.desiredKey.level, lhs.desiredKey.y, lhs.desiredKey.x)
            < std::tie(rhs.desiredKey.level, rhs.desiredKey.y, rhs.desiredKey.x);
    });
    return std::move(state.frame);
}

void VisualCoverBuilder::visit(const ViewportRequest &request,
                               const TileCache &tileCache,
                               const CommittedVisualFrame *previous,
                               const TileKey &key,
                               const int leafLevel,
                               BuildState &state)
{
    if (!tileIntersectsViewport(key, request))
    {
        return;
    }

    if (const auto *uniform = tileCache.findNearestUniformAncestorOrSelf(key, request.formula.semanticsHash))
    {
        emitUniformAuthority(request, *uniform, state);
        return;
    }

    const auto *record = tileCache.find(key, request.formula.semanticsHash);
    const auto hasPartialCover = shouldSplitForPartialCover(
        tileCache,
        previous,
        key,
        request.formula.semanticsHash);

    if (key.level <= leafLevel)
    {
        if (record)
        {
            if (auto tile = currentReadyTileFor(request, key, *record, false, state))
            {
                state.frame.tiles.push_back(*tile);
                return;
            }
            queuePreloadTile(request, key, *record, state);
        }

        if (hasPartialCover && key.level > LowestFiniteTileLevel)
        {
            for (const auto &child : tileChildren(key))
            {
                visit(request, tileCache, previous, child, leafLevel, state);
            }
            return;
        }

        emitFallbackCell(request, tileCache, key, state);
        return;
    }

    if (record && record->valueState == TileValueState::Mixed)
    {
        const auto regionReady = record->regionPixels.has_value();
        const auto presentable = mixedRegionPresentable(*record, state);
        if (regionReady && !presentable)
        {
            queuePreloadTile(request, key, *record, state);
        }

        if (hasPartialCover || !regionReady)
        {
            for (const auto &child : tileChildren(key))
            {
                visit(request, tileCache, previous, child, leafLevel, state);
            }
            return;
        }

        if (!presentable)
        {
            emitFallbackCell(request, tileCache, key, state);
            return;
        }

        if (regionReady && !hasPartialCover)
        {
            if (auto tile = currentReadyTileFor(request, key, *record, false, state))
            {
                state.frame.tiles.push_back(*tile);
                return;
            }
        }
    }
    else if (record && (record->valueState == TileValueState::UniformTrue
                        || record->valueState == TileValueState::UniformFalse))
    {
        if (auto tile = currentReadyTileFor(request, key, *record, false, state))
        {
            state.frame.tiles.push_back(*tile);
            return;
        }
    }

    if (hasPartialCover)
    {
        for (const auto &child : tileChildren(key))
        {
            visit(request, tileCache, previous, child, leafLevel, state);
        }
        return;
    }

    emitFallbackCell(request, tileCache, key, state);
}

void VisualCoverBuilder::emitFallbackCell(const ViewportRequest &request,
                                          const TileCache &tileCache,
                                          const TileKey &key,
                                          BuildState &state)
{
    if (auto previousTile = previousVisualTileFor(request, key, state.previous))
    {
        state.frame.tiles.push_back(*previousTile);
        return;
    }

    if (const auto *ancestor = tileCache.findNearestRenderableMixedAncestor(key, request.formula.semanticsHash))
    {
        if (auto tile = mixedAncestorFallbackTile(request, key, *ancestor, state))
        {
            state.frame.tiles.push_back(*tile);
            return;
        }
    }

    state.frame.tiles.push_back(missingTileFor(request, key));
}

void VisualCoverBuilder::emitUniformAuthority(const ViewportRequest &request,
                                              const TileRecord &record,
                                              BuildState &state)
{
    if (!state.emittedUniformAuthorities.insert(record.key).second)
    {
        return;
    }

    if (auto tile = currentReadyTileFor(request, record.key, record, false, state))
    {
        state.frame.tiles.push_back(*tile);
    }
}

void VisualCoverBuilder::queuePreloadTile(const ViewportRequest &request,
                                          const TileKey &displayKey,
                                          const TileRecord &record,
                                          BuildState &state)
{
    if (record.valueState != TileValueState::Mixed || !record.regionPixels)
    {
        return;
    }
    if (!state.preloadedRegions.insert(record.regionPixels->id).second)
    {
        return;
    }

    const auto bounds = intersectRect(tileBounds(displayKey), request.xRange, request.yRange);
    if (!validRect(bounds))
    {
        return;
    }

    state.frame.preloadTiles.push_back(DisplayTile{
        .desiredKey = displayKey,
        .sourceKey = record.key,
        .worldBounds = bounds,
        .visualState = TileVisualState::MixedRegion,
        .cpuRegion = record.regionPixels,
        .uvRect = uvRectForTileSource(bounds, tileBounds(record.key))
    });
}

bool VisualCoverBuilder::mixedRegionPresentable(const TileRecord &record,
                                                const BuildState &state)
{
    if (record.valueState != TileValueState::Mixed || !record.regionPixels)
    {
        return false;
    }
    return !state.regionPresentable || state.regionPresentable(*record.regionPixels);
}

bool VisualCoverBuilder::shouldSplitForPartialCover(const TileCache &tileCache,
                                                    const CommittedVisualFrame *previous,
                                                    const TileKey &key,
                                                    const FormulaSemanticsHash semanticsHash)
{
    return tileCache.hasDescendantRecord(key, semanticsHash)
        || hasPreviousVisualDescendant(key, previous);
}

bool VisualCoverBuilder::hasPreviousVisualDescendant(
    const TileKey &key,
    const CommittedVisualFrame *previous)
{
    if (!previous)
    {
        return false;
    }

    return std::ranges::any_of(previous->tiles, [key](const DisplayTile &tile)
    {
        return presentableCommittedTile(tile)
            && parentCoversChild(key, tile.desiredKey);
    });
}

std::optional<DisplayTile> VisualCoverBuilder::currentReadyTileFor(
    const ViewportRequest &request,
    const TileKey &displayKey,
    const TileRecord &record,
    const bool fallback,
    const BuildState &state)
{
    TileVisualState visualState = TileVisualState::Missing;
    switch (record.valueState)
    {
    case TileValueState::UniformTrue:
        visualState = TileVisualState::UniformTrue;
        break;
    case TileValueState::UniformFalse:
        visualState = TileVisualState::UniformFalse;
        break;
    case TileValueState::Mixed:
        if (!mixedRegionPresentable(record, state))
        {
            return std::nullopt;
        }
        visualState = TileVisualState::MixedRegion;
        break;
    case TileValueState::Unknown:
    default:
        return std::nullopt;
    }

    const auto bounds = intersectRect(tileBounds(displayKey), request.xRange, request.yRange);
    if (!validRect(bounds))
    {
        return std::nullopt;
    }

    auto uvRect = std::array{0.0f, 0.0f, 1.0f, 1.0f};
    if (visualState == TileVisualState::MixedRegion)
    {
        uvRect = uvRectForTileSource(bounds, tileBounds(record.key));
    }

    return DisplayTile{
        .desiredKey = displayKey,
        .sourceKey = record.key,
        .worldBounds = bounds,
        .visualState = visualState,
        .cpuRegion = record.regionPixels,
        .uvRect = uvRect,
        .isFallback = fallback,
        .clippedFallback = fallback && record.key != displayKey
    };
}

std::optional<DisplayTile> VisualCoverBuilder::previousVisualTileFor(
    const ViewportRequest &request,
    const TileKey &displayKey,
    const CommittedVisualFrame *previous)
{
    if (!previous)
    {
        return std::nullopt;
    }

    const DisplayTile *best = nullptr;
    for (const auto &tile : previous->tiles)
    {
        if (!presentableCommittedTile(tile) || !coversDisplayKey(tile.desiredKey, displayKey))
        {
            continue;
        }

        if (!best
            || tile.desiredKey.level < best->desiredKey.level
            || (tile.desiredKey.level == best->desiredKey.level
                && tile.desiredKey == displayKey
                && best->desiredKey != displayKey))
        {
            best = &tile;
        }
    }

    if (!best)
    {
        return std::nullopt;
    }

    const auto bounds = intersectRect(tileBounds(displayKey), request.xRange, request.yRange);
    if (!validRect(bounds))
    {
        return std::nullopt;
    }

    auto tile = *best;
    tile.desiredKey = displayKey;
    tile.worldBounds = bounds;
    tile.isFallback = true;
    tile.clippedFallback = best->desiredKey != displayKey || best->sourceKey != displayKey;
    if (tile.visualState == TileVisualState::MixedRegion)
    {
        tile.uvRect = uvRectForTileSource(bounds, tileBounds(tile.sourceKey));
    }
    else
    {
        tile.uvRect = {0.0f, 0.0f, 1.0f, 1.0f};
    }
    return tile;
}

std::optional<DisplayTile> VisualCoverBuilder::mixedAncestorFallbackTile(
    const ViewportRequest &request,
    const TileKey &displayKey,
    const TileRecord &record,
    BuildState &state)
{
    if (record.valueState != TileValueState::Mixed || !record.regionPixels)
    {
        return std::nullopt;
    }
    if (!mixedRegionPresentable(record, state))
    {
        queuePreloadTile(request, displayKey, record, state);
        return std::nullopt;
    }

    const auto bounds = intersectRect(tileBounds(displayKey), request.xRange, request.yRange);
    if (!validRect(bounds))
    {
        return std::nullopt;
    }

    return DisplayTile{
        .desiredKey = displayKey,
        .sourceKey = record.key,
        .worldBounds = bounds,
        .visualState = TileVisualState::MixedRegion,
        .cpuRegion = record.regionPixels,
        .uvRect = uvRectForTileSource(bounds, tileBounds(record.key)),
        .isFallback = true,
        .clippedFallback = true
    };
}

DisplayTile VisualCoverBuilder::missingTileFor(const ViewportRequest &request, const TileKey &key)
{
    return {
        .desiredKey = key,
        .sourceKey = key,
        .worldBounds = intersectRect(tileBounds(key), request.xRange, request.yRange),
        .visualState = TileVisualState::Missing,
        .isFallback = true,
        .clippedFallback = false
    };
}
}
