#include "catch.hpp"

#include <algorithm>
#include <vector>

#include "../src/Compute/VisualCoverBuilder.h"
#include "../src/Formula/FormulaCompiler.h"
#include "../src/Tile/TileMath.h"

namespace
{
gx::ViewportRequest requestFor(const gx::CompiledFormulaHandle &formula,
                               const Interval &xRange,
                               const Interval &yRange,
                               const uint64_t requestId = 1)
{
    return {
        .header = {.requestId = requestId, .generation = 1},
        .formula = formula,
        .xRange = xRange,
        .yRange = yRange,
        .framebufferWidth = 800,
        .framebufferHeight = 800,
        .devicePixelRatio = 1.0
    };
}

gx::TileTransaction mixedNeedsRegionTransaction(const gx::FormulaSemanticsHash semantics,
                                                const gx::TileKey &key)
{
    return {
        .header = {.requestId = 1, .generation = 1},
        .semanticsHash = semantics,
        .deltas = {
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::IntervalQueued,
                .classification = gx::TileClassification::Unknown
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::IntervalReady,
                .classification = gx::TileClassification::Mixed,
                .interval = Interval{-1.0, 1.0}
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::MixedNeedsRegion,
                .classification = gx::TileClassification::Mixed,
                .interval = Interval{-1.0, 1.0}
            }
        }
    };
}

gx::TileTransaction mixedRegionTransaction(const gx::FormulaSemanticsHash semantics,
                                           const gx::TileKey &key)
{
    return {
        .header = {.requestId = 1, .generation = 1},
        .semanticsHash = semantics,
        .deltas = {
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::IntervalQueued,
                .classification = gx::TileClassification::Unknown
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::IntervalReady,
                .classification = gx::TileClassification::Mixed,
                .interval = Interval{-1.0, 1.0}
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::MixedNeedsRegion,
                .classification = gx::TileClassification::Mixed,
                .interval = Interval{-1.0, 1.0}
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::RegionQueued,
                .classification = gx::TileClassification::Mixed
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::RegionReady,
                .classification = gx::TileClassification::Mixed,
                .region = gx::RegionImageRef{.id = 10, .width = 256, .height = 256}
            }
        }
    };
}

gx::TileTransaction uniformTrueTransaction(const gx::FormulaSemanticsHash semantics,
                                           const gx::TileKey &key)
{
    return {
        .header = {.requestId = 1, .generation = 1},
        .semanticsHash = semantics,
        .deltas = {
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::IntervalQueued,
                .classification = gx::TileClassification::Unknown
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::IntervalReady,
                .classification = gx::TileClassification::UniformTrue,
                .interval = Interval{1.0, 1.0}
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::UniformTrue,
                .classification = gx::TileClassification::UniformTrue,
                .interval = Interval{1.0, 1.0}
            }
        }
    };
}

gx::DisplayTile committedUniformTile(const gx::TileKey &key)
{
    return {
        .desiredKey = key,
        .sourceKey = key,
        .worldBounds = gx::tileBounds(key),
        .visualState = gx::TileVisualState::UniformTrue
    };
}

bool overlaps(const gx::Rect &lhs, const gx::Rect &rhs)
{
    return lhs.xMin < rhs.xMax
        && lhs.xMax > rhs.xMin
        && lhs.yMin < rhs.yMax
        && lhs.yMax > rhs.yMin;
}
}

TEST_CASE("VisualCoverBuilder keeps partial previous cover during zoom-out", "[VisualCoverBuilder]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x>y");
    REQUIRE(formula.diagnostics.ok);

    const auto request = requestFor(
        formula.handle,
        Interval{-1024.0, 1024.0},
        Interval{-1024.0, 1024.0});
    REQUIRE(gx::seedTileLevelForViewport(request) == 10);

    const gx::TileKey seed{-1, -1, 10};
    const auto previousChild = gx::tileChildren(seed).front();
    const gx::CommittedVisualFrame previous{
        .semantics = formula.handle.semanticsHash,
        .viewport = request,
        .tiles = {committedUniformTile(previousChild)}
    };

    gx::TileCache cache;
    const auto frame = gx::VisualCoverBuilder{}.build(
        request,
        cache,
        &previous,
        4,
        1);

    const auto previousIt = std::ranges::find_if(frame.tiles, [previousChild](const gx::DisplayTile &tile)
    {
        return tile.desiredKey == previousChild
            && tile.sourceKey == previousChild
            && tile.visualState == gx::TileVisualState::UniformTrue
            && tile.isFallback;
    });
    REQUIRE(previousIt != frame.tiles.end());

    CHECK(std::ranges::none_of(frame.tiles, [seed](const gx::DisplayTile &tile)
    {
        return tile.desiredKey == seed && tile.visualState == gx::TileVisualState::Missing;
    }));
    CHECK(std::ranges::any_of(frame.tiles, [](const gx::DisplayTile &tile)
    {
        return tile.visualState == gx::TileVisualState::Missing;
    }));

    for (const auto &tile : frame.tiles)
    {
        if (tile.visualState == gx::TileVisualState::Missing)
        {
            CHECK_FALSE(overlaps(tile.worldBounds, previousIt->worldBounds));
        }
    }
}

TEST_CASE("VisualCoverBuilder lets current uniform authority replace stale previous children", "[VisualCoverBuilder]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x=x");
    REQUIRE(formula.diagnostics.ok);

    const auto request = requestFor(
        formula.handle,
        Interval{-1024.0, 0.0},
        Interval{-1024.0, 0.0});

    const gx::TileKey authority{-1, -1, 10};
    const auto staleChild = gx::tileChildren(authority).front();
    const gx::CommittedVisualFrame previous{
        .semantics = formula.handle.semanticsHash,
        .viewport = request,
        .tiles = {committedUniformTile(staleChild)}
    };

    gx::TileCache cache;
    REQUIRE(cache.apply(uniformTrueTransaction(formula.handle.semanticsHash, authority)).rejected == 0);

    const auto frame = gx::VisualCoverBuilder{}.build(request, cache, &previous);

    REQUIRE(frame.tiles.size() == 1);
    CHECK(frame.tiles.front().desiredKey == authority);
    CHECK(frame.tiles.front().sourceKey == authority);
    CHECK(frame.tiles.front().visualState == gx::TileVisualState::UniformTrue);
    CHECK_FALSE(frame.tiles.front().isFallback);
}

TEST_CASE("VisualCoverBuilder keeps previous children until the current leaf tile is ready", "[VisualCoverBuilder]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x>y");
    REQUIRE(formula.diagnostics.ok);

    const auto request = requestFor(
        formula.handle,
        Interval{0.0, 4.0},
        Interval{0.0, 4.0});
    REQUIRE(gx::leafTileLevel(request) == 0);

    const gx::TileKey mixedLeaf{0, 0, 0};
    std::vector<gx::DisplayTile> previousTiles;
    for (const auto &child : gx::tileChildren(mixedLeaf))
    {
        previousTiles.push_back(committedUniformTile(child));
    }
    const gx::CommittedVisualFrame previous{
        .semantics = formula.handle.semanticsHash,
        .viewport = request,
        .tiles = previousTiles
    };

    gx::TileCache cache;
    REQUIRE(cache.apply(mixedNeedsRegionTransaction(formula.handle.semanticsHash, mixedLeaf)).rejected == 0);

    const auto seedLevel = gx::seedTileLevelForViewport(request);
    const auto frame = gx::VisualCoverBuilder{}.build(
        request,
        cache,
        &previous,
        4,
        seedLevel - mixedLeaf.level);

    CHECK(std::ranges::none_of(frame.tiles, [mixedLeaf](const gx::DisplayTile &tile)
    {
        return tile.desiredKey == mixedLeaf && tile.visualState == gx::TileVisualState::Missing;
    }));
    for (const auto &previousTile : previousTiles)
    {
        CHECK(std::ranges::any_of(frame.tiles, [previousTile](const gx::DisplayTile &tile)
        {
            return tile.desiredKey == previousTile.desiredKey
                && tile.visualState == gx::TileVisualState::UniformTrue
                && tile.isFallback;
        }));
    }
}

TEST_CASE("VisualCoverBuilder preserves mixed-region UVs when viewport clips the source tile", "[VisualCoverBuilder]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x<=y");
    REQUIRE(formula.diagnostics.ok);

    const gx::TileKey source{1, 1, 5};
    const auto request = requestFor(
        formula.handle,
        Interval{40.0, 56.0},
        Interval{36.0, 60.0});
    REQUIRE(gx::seedTileLevelForViewport(request, 1) == source.level);

    gx::TileCache cache;
    REQUIRE(cache.apply(mixedRegionTransaction(formula.handle.semanticsHash, source)).rejected == 0);

    const auto frame = gx::VisualCoverBuilder{}.build(request, cache, nullptr, 1);

    REQUIRE(frame.tiles.size() == 1);
    const auto &tile = frame.tiles.front();
    CHECK(tile.desiredKey == source);
    CHECK(tile.sourceKey == source);
    CHECK(tile.worldBounds == gx::Rect{40.0, 56.0, 36.0, 60.0});
    CHECK(tile.visualState == gx::TileVisualState::MixedRegion);
    CHECK(tile.uvRect[0] == 0.25f);
    CHECK(tile.uvRect[1] == 0.125f);
    CHECK(tile.uvRect[2] == 0.75f);
    CHECK(tile.uvRect[3] == 0.875f);
}

TEST_CASE("VisualCoverBuilder replaces stale descendants at the current leaf level", "[VisualCoverBuilder]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x<=y");
    REQUIRE(formula.diagnostics.ok);

    const auto request = requestFor(
        formula.handle,
        Interval{0.0, 1024.0},
        Interval{0.0, 1024.0});
    constexpr auto refinementDepth = 1;
    const auto seedLevel = gx::seedTileLevelForViewport(request);
    const auto leafLevel = gx::leafTileLevelForSeed(seedLevel, refinementDepth);
    REQUIRE(seedLevel == 9);
    REQUIRE(leafLevel == 8);

    const gx::TileKey seed{0, 0, 9};
    const gx::TileKey leaf{0, 0, leafLevel};
    const auto staleChild = gx::tileChildren(leaf).front();

    gx::TileCache cache;
    REQUIRE(cache.apply(mixedNeedsRegionTransaction(formula.handle.semanticsHash, seed)).rejected == 0);
    REQUIRE(cache.apply(mixedRegionTransaction(formula.handle.semanticsHash, leaf)).rejected == 0);
    REQUIRE(cache.apply(mixedRegionTransaction(formula.handle.semanticsHash, staleChild)).rejected == 0);

    const gx::CommittedVisualFrame previous{
        .semantics = formula.handle.semanticsHash,
        .viewport = request,
        .tiles = {
            gx::DisplayTile{
                .desiredKey = staleChild,
                .sourceKey = staleChild,
                .worldBounds = gx::tileBounds(staleChild),
                .visualState = gx::TileVisualState::UniformTrue
            }
        }
    };

    const auto frame = gx::VisualCoverBuilder{}.build(request, cache, &previous, 4, refinementDepth);

    CHECK(std::ranges::any_of(frame.tiles, [leaf](const gx::DisplayTile &tile)
    {
        return tile.desiredKey == leaf
            && tile.sourceKey == leaf
            && tile.visualState == gx::TileVisualState::MixedRegion
            && !tile.isFallback;
    }));
    CHECK(std::ranges::none_of(frame.tiles, [leafLevel](const gx::DisplayTile &tile)
    {
        return tile.desiredKey.level < leafLevel;
    }));
}

TEST_CASE("VisualCoverBuilder keeps huge zoom-out viewports covered above the old fixed level cap",
          "[VisualCoverBuilder]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x<=y");
    REQUIRE(formula.diagnostics.ok);

    const auto request = requestFor(
        formula.handle,
        Interval{-1.0e12, 1.0e12},
        Interval{-1.0e12, 1.0e12});
    const auto seedLevel = gx::seedTileLevelForViewport(request);
    REQUIRE(seedLevel > 30);

    gx::TileCache cache;
    const auto frame = gx::VisualCoverBuilder{}.build(request, cache);

    REQUIRE_FALSE(frame.tiles.empty());
    CHECK(std::ranges::all_of(frame.tiles, [seedLevel](const gx::DisplayTile &tile)
    {
        return tile.desiredKey.level == seedLevel
            && tile.visualState == gx::TileVisualState::Missing;
    }));
}
