#include "catch.hpp"

#include <algorithm>
#include <unordered_set>

#include "../src/App/PresentationPlanner.h"
#include "../src/Formula/FormulaCompiler.h"

namespace
{
gx::ViewportRequest requestFor(const gx::CompiledFormulaHandle &formula)
{
    return {
        .header = {.requestId = 1, .generation = 1},
        .formula = formula,
        .xRange = Interval{0.0, 4.0},
        .yRange = Interval{0.0, 4.0},
        .framebufferWidth = 800,
        .framebufferHeight = 800,
        .devicePixelRatio = 1.0
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

gx::DisplayTile committedUniformTile(const gx::TileKey &key)
{
    return {
        .desiredKey = key,
        .sourceKey = key,
        .worldBounds = gx::tileBounds(key),
        .visualState = gx::TileVisualState::UniformTrue
    };
}

gx::TextureSlice sliceFor(const std::unordered_set<uint64_t> &residentIds,
                          const gx::RegionImageRef &ref)
{
    return residentIds.contains(ref.id)
        ? gx::TextureSlice{.textureId = 1, .slice = static_cast<uint32_t>(ref.id)}
        : gx::TextureSlice{};
}
}

TEST_CASE("PresentationPlanner treats nonresident mixed replacements as preload only",
          "[PresentationPlanner]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x>y");
    REQUIRE(formula.diagnostics.ok);
    const auto request = requestFor(formula.handle);
    const gx::TileKey mixedLeaf{0, 0, 0};

    gx::TileCache cache;
    REQUIRE(cache.apply(mixedRegionTransaction(formula.handle.semanticsHash, mixedLeaf)).rejected == 0);

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

    const auto seedLevel = gx::seedTileLevelForViewport(request);
    const auto plan = gx::PresentationPlanner{}.plan(
        gx::PresentationPlanRequest{
            .viewport = request,
            .tileCache = cache,
            .previous = &previous,
            .maxSeedCells = 4,
            .refinementDepth = seedLevel - mixedLeaf.level
        },
        [](const gx::RegionImageRef &ref)
        {
            return sliceFor({}, ref);
        });

    CHECK(plan.visibleRegions.empty());
    CHECK(plan.committedTiles.size() == previousTiles.size());
    REQUIRE(plan.preloadTiles.size() == 1);
    CHECK(plan.preloadTiles.front().sourceKey == mixedLeaf);
    CHECK(plan.uploadPlan.textureUploads == std::vector<gx::TileKey>{mixedLeaf});
    CHECK(std::ranges::none_of(plan.displayTiles, [mixedLeaf](const gx::DisplayTile &tile)
    {
        return tile.sourceKey == mixedLeaf && tile.visualState == gx::TileVisualState::MixedRegion;
    }));
    for (const auto &previousTile : previousTiles)
    {
        CHECK(std::ranges::any_of(plan.displayTiles, [previousTile](const gx::DisplayTile &tile)
        {
            return tile.desiredKey == previousTile.desiredKey
                && tile.visualState == gx::TileVisualState::UniformTrue
                && tile.isFallback;
        }));
    }
}

TEST_CASE("PresentationPlanner commits mixed replacements only after residency",
          "[PresentationPlanner]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x>y");
    REQUIRE(formula.diagnostics.ok);
    const auto request = requestFor(formula.handle);
    const gx::TileKey mixedLeaf{0, 0, 0};

    gx::TileCache cache;
    REQUIRE(cache.apply(mixedRegionTransaction(formula.handle.semanticsHash, mixedLeaf)).rejected == 0);

    const auto seedLevel = gx::seedTileLevelForViewport(request);
    const auto plan = gx::PresentationPlanner{}.plan(
        gx::PresentationPlanRequest{
            .viewport = request,
            .tileCache = cache,
            .maxSeedCells = 4,
            .refinementDepth = seedLevel - mixedLeaf.level
        },
        [](const gx::RegionImageRef &ref)
        {
            return sliceFor({10}, ref);
        });

    CHECK(std::ranges::any_of(plan.displayTiles, [mixedLeaf](const gx::DisplayTile &tile)
    {
        return tile.sourceKey == mixedLeaf
            && tile.visualState == gx::TileVisualState::MixedRegion
            && tile.gpuSlice.textureId == 1;
    }));
    CHECK(std::ranges::any_of(plan.visibleRegions, [](const gx::RegionImageRef &ref)
    {
        return ref.id == 10;
    }));
    CHECK(plan.preloadTiles.empty());
    CHECK(std::ranges::any_of(plan.committedTiles, [mixedLeaf](const gx::DisplayTile &tile)
    {
        return tile.sourceKey == mixedLeaf && tile.visualState == gx::TileVisualState::MixedRegion;
    }));
}
