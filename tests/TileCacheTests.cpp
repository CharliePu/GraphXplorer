#include "catch.hpp"

#include "../src/Tile/TileCache.h"

TEST_CASE("TileCache accepts only valid state transitions", "[TileCache]")
{
    CHECK(gx::TileCache::validTransition(gx::TileStage::Unknown, gx::TileStage::IntervalQueued));
    CHECK(gx::TileCache::validTransition(gx::TileStage::IntervalReady, gx::TileStage::MixedNeedsRegion));
    CHECK_FALSE(gx::TileCache::validTransition(gx::TileStage::Unknown, gx::TileStage::GpuResident));
}

TEST_CASE("TileCache applies transactions atomically and rejects stale generations", "[TileCache]")
{
    gx::TileCache cache;
    const gx::FormulaSemanticsHash semantics{123};
    const gx::TileKey key{1, 2, 3};

    gx::TileTransaction tx{
        .header = {.requestId = 1, .generation = 2},
        .semanticsHash = semantics,
        .deltas = {
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 2},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::IntervalQueued
            }
        }
    };

    auto result = cache.apply(tx);
    CHECK(result.applied == 1);
    CHECK(result.rejected == 0);

    tx.header.generation = 1;
    tx.deltas.front().header.generation = 1;
    tx.deltas.front().stage = gx::TileStage::IntervalReady;
    result = cache.apply(tx);
    CHECK(result.applied == 0);
    CHECK(result.rejected == 1);
}
