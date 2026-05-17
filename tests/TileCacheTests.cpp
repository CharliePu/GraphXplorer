#include "catch.hpp"

#include <algorithm>
#include <array>

#include "../src/Tile/TileCache.h"
#include "../src/Tile/TileMath.h"

namespace
{
gx::TileTransaction mixedRegionTransaction(const gx::FormulaSemanticsHash semantics,
                                           const gx::TileKey &key,
                                           const uint64_t generation)
{
    return {
        .header = {.requestId = 1, .generation = generation},
        .semanticsHash = semantics,
        .deltas = {
            gx::TileDelta{
                .header = {.requestId = 1, .generation = generation},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::IntervalQueued,
                .classification = gx::TileClassification::Unknown
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = generation},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::IntervalReady,
                .classification = gx::TileClassification::Mixed,
                .interval = Interval{-1.0, 1.0}
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = generation},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::MixedNeedsRegion,
                .classification = gx::TileClassification::Mixed,
                .interval = Interval{-1.0, 1.0}
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = generation},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::RegionQueued,
                .classification = gx::TileClassification::Mixed
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = generation},
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
                                           const gx::TileKey &key,
                                           const uint64_t generation)
{
    return {
        .header = {.requestId = 1, .generation = generation},
        .semanticsHash = semantics,
        .deltas = {
            gx::TileDelta{
                .header = {.requestId = 1, .generation = generation},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::IntervalQueued,
                .classification = gx::TileClassification::Unknown
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = generation},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::IntervalReady,
                .classification = gx::TileClassification::UniformTrue,
                .interval = Interval{1.0, 2.0}
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = generation},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::UniformTrue,
                .classification = gx::TileClassification::UniformTrue,
                .interval = Interval{1.0, 2.0}
            }
        }
    };
}

gx::TileTransaction uniformFalseTransaction(const gx::FormulaSemanticsHash semantics,
                                            const gx::TileKey &key,
                                            const uint64_t generation)
{
    return {
        .header = {.requestId = 1, .generation = generation},
        .semanticsHash = semantics,
        .deltas = {
            gx::TileDelta{
                .header = {.requestId = 1, .generation = generation},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::IntervalQueued,
                .classification = gx::TileClassification::Unknown
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = generation},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::IntervalReady,
                .classification = gx::TileClassification::UniformFalse,
                .interval = Interval{0.0, 0.0}
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = generation},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::UniformFalse,
                .classification = gx::TileClassification::UniformFalse,
                .interval = Interval{0.0, 0.0}
            }
        }
    };
}
}

TEST_CASE("TileCache accepts only valid state transitions", "[TileCache]")
{
    CHECK(gx::TileCache::validTransition(gx::TileStage::Unknown, gx::TileStage::IntervalQueued));
    CHECK(gx::TileCache::validTransition(gx::TileStage::IntervalQueued, gx::TileStage::Unknown));
    CHECK(gx::TileCache::validTransition(gx::TileStage::IntervalReady, gx::TileStage::MixedNeedsRegion));
    CHECK(gx::TileCache::validTransition(gx::TileStage::RegionQueued, gx::TileStage::MixedNeedsRegion));
    CHECK_FALSE(gx::TileCache::validTransition(gx::TileStage::Unknown, gx::TileStage::GpuResident));
}

TEST_CASE("TileCache recovers queued work into schedulable states", "[TileCache]")
{
    gx::TileCache cache;
    const gx::FormulaSemanticsHash semantics{122};
    const gx::TileKey intervalKey{1, 2, 3};
    const gx::TileKey regionKey{2, 3, 3};

    REQUIRE(cache.transition(intervalKey, semantics, gx::TileStage::IntervalQueued));
    REQUIRE(cache.apply(gx::TileTransaction{
        .header = {.requestId = 1, .generation = 2},
        .semanticsHash = semantics,
        .deltas = {
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 2},
                .semanticsHash = semantics,
                .key = regionKey,
                .stage = gx::TileStage::IntervalQueued
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 2},
                .semanticsHash = semantics,
                .key = regionKey,
                .stage = gx::TileStage::IntervalReady,
                .classification = gx::TileClassification::Mixed,
                .interval = Interval{-1.0, 1.0}
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 2},
                .semanticsHash = semantics,
                .key = regionKey,
                .stage = gx::TileStage::MixedNeedsRegion,
                .classification = gx::TileClassification::Mixed,
                .interval = Interval{-1.0, 1.0}
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 2},
                .semanticsHash = semantics,
                .key = regionKey,
                .stage = gx::TileStage::RegionQueued,
                .classification = gx::TileClassification::Mixed
            }
        }
    }).rejected == 0);

    const auto recovered = cache.recoverQueuedWork(semantics);
    CHECK(recovered.intervalQueued == 1);
    CHECK(recovered.regionQueued == 1);

    const auto *intervalRecord = cache.find(intervalKey, semantics);
    REQUIRE(intervalRecord != nullptr);
    CHECK(intervalRecord->valueState == gx::TileValueState::Unknown);
    CHECK(intervalRecord->workState == gx::TileWorkState::Idle);

    const auto *regionRecord = cache.find(regionKey, semantics);
    REQUIRE(regionRecord != nullptr);
    CHECK(regionRecord->valueState == gx::TileValueState::Mixed);
    CHECK(regionRecord->workState == gx::TileWorkState::Idle);
    REQUIRE(regionRecord->interval.has_value());
    CHECK(regionRecord->interval->lower == -1.0);
    CHECK(regionRecord->interval->upper == 1.0);
}

TEST_CASE("TileCache applies transactions by formula semantics instead of generation", "[TileCache]")
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
    tx.deltas.front().classification = gx::TileClassification::UniformTrue;
    tx.deltas.front().interval = Interval{1.0, 1.0};
    result = cache.apply(tx);
    CHECK(result.applied == 1);
    CHECK(result.rejected == 0);

    const auto *record = cache.find(key, semantics);
    REQUIRE(record != nullptr);
    CHECK(record->valueState == gx::TileValueState::UniformTrue);
}

TEST_CASE("TileCache rejects invalid transactions without committing earlier deltas", "[TileCache]")
{
    gx::TileCache cache;
    const gx::FormulaSemanticsHash semantics{124};
    const gx::TileKey key{2, 3, 4};

    const gx::TileTransaction tx{
        .header = {.requestId = 1, .generation = 1},
        .semanticsHash = semantics,
        .deltas = {
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::IntervalQueued
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::RegionReady,
                .classification = gx::TileClassification::Mixed,
                .region = gx::RegionImageRef{.id = 99, .width = 256, .height = 256}
            }
        }
    };

    const auto result = cache.apply(tx);
    CHECK(result.applied == 0);
    CHECK(result.rejected == tx.deltas.size());
    CHECK(cache.find(key, semantics) == nullptr);
    CHECK(cache.size() == 0);
}

TEST_CASE("TileCache promotes agreeing uniform children into the parent authority", "[TileCache]")
{
    gx::TileCache cache;
    const gx::FormulaSemanticsHash semantics{789};
    const gx::TileKey parent{0, 0, 5};

    auto result = cache.apply(mixedRegionTransaction(semantics, parent, 1));
    REQUIRE(result.rejected == 0);
    for (const auto &child : gx::tileChildren(parent))
    {
        result = cache.apply(uniformTrueTransaction(semantics, child, 1));
        REQUIRE(result.rejected == 0);
    }

    const auto *parentRecord = cache.find(parent, semantics);
    REQUIRE(parentRecord != nullptr);
    CHECK(parentRecord->valueState == gx::TileValueState::UniformTrue);
    CHECK(parentRecord->workState == gx::TileWorkState::Idle);
    CHECK_FALSE(parentRecord->regionPixels.has_value());
    for (const auto &child : gx::tileChildren(parent))
    {
        CHECK(cache.find(child, semantics) == nullptr);
    }
    CHECK(cache.size() == 1);
}

TEST_CASE("TileCache prunes stale descendants when a parent is directly classified uniform", "[TileCache]")
{
    gx::TileCache cache;
    const gx::FormulaSemanticsHash semantics{790};
    const gx::TileKey parent{0, 0, 5};
    const gx::TileKey child{0, 0, 4};

    auto result = cache.apply(mixedRegionTransaction(semantics, child, 1));
    REQUIRE(result.rejected == 0);
    REQUIRE(cache.find(child, semantics) != nullptr);

    result = cache.apply(uniformFalseTransaction(semantics, parent, 2));
    REQUIRE(result.rejected == 0);

    const auto *parentRecord = cache.find(parent, semantics);
    REQUIRE(parentRecord != nullptr);
    CHECK(parentRecord->valueState == gx::TileValueState::UniformFalse);
    CHECK(parentRecord->workState == gx::TileWorkState::Idle);
    CHECK(cache.find(child, semantics) == nullptr);
    CHECK(cache.size() == 1);
}

TEST_CASE("TileCache rejects descendants under an existing uniform authority", "[TileCache]")
{
    gx::TileCache cache;
    const gx::FormulaSemanticsHash semantics{793};
    const gx::TileKey parent{0, 0, 5};
    const gx::TileKey child{0, 0, 4};

    auto result = cache.apply(uniformTrueTransaction(semantics, parent, 1));
    REQUIRE(result.rejected == 0);
    REQUIRE(cache.find(parent, semantics) != nullptr);

    result = cache.apply(mixedRegionTransaction(semantics, child, 2));
    CHECK(result.applied == 0);
    CHECK(result.rejected == 5);
    CHECK(cache.find(parent, semantics) != nullptr);
    CHECK(cache.find(child, semantics) == nullptr);
    CHECK(cache.size() == 1);

    result = cache.apply(uniformFalseTransaction(semantics, child, 3));
    CHECK(result.applied == 0);
    CHECK(result.rejected == 3);
    CHECK(cache.find(parent, semantics) != nullptr);
    CHECK(cache.find(child, semantics) == nullptr);
    CHECK(cache.size() == 1);
}

TEST_CASE("TileCache transition rejects descendants under an existing uniform authority", "[TileCache]")
{
    gx::TileCache cache;
    const gx::FormulaSemanticsHash semantics{796};
    const gx::TileKey parent{0, 0, 5};
    const gx::TileKey child{0, 0, 4};

    auto result = cache.apply(uniformTrueTransaction(semantics, parent, 1));
    REQUIRE(result.rejected == 0);

    CHECK_FALSE(cache.transition(child, semantics, gx::TileStage::IntervalQueued));
    CHECK(cache.find(parent, semantics) != nullptr);
    CHECK(cache.find(child, semantics) == nullptr);
    CHECK(cache.size() == 1);
}

TEST_CASE("TileCache tracks occupied levels and prunes shadowed sparse descendants", "[TileCache]")
{
    gx::TileCache cache;
    const gx::FormulaSemanticsHash semantics{795};
    const gx::TileKey mixedRoot{-1, -1, 10};
    const gx::TileKey uniformAuthority{-1, -1, 8};
    const gx::TileKey intermediate{-1, -1, 5};
    const gx::TileKey candidate{-1, -1, 4};

    auto result = cache.apply(mixedRegionTransaction(semantics, mixedRoot, 1));
    REQUIRE(result.rejected == 0);
    result = cache.apply(mixedRegionTransaction(semantics, intermediate, 1));
    REQUIRE(result.rejected == 0);
    result = cache.apply(mixedRegionTransaction(semantics, candidate, 1));
    REQUIRE(result.rejected == 0);
    CHECK(cache.occupiedLevelsForFormula(semantics) == std::vector<int>{4, 5, 10});

    result = cache.apply(uniformTrueTransaction(semantics, uniformAuthority, 2));
    REQUIRE(result.rejected == 0);

    CHECK(cache.find(uniformAuthority, semantics) != nullptr);
    CHECK(cache.find(mixedRoot, semantics) != nullptr);
    CHECK(cache.find(intermediate, semantics) == nullptr);
    CHECK(cache.find(candidate, semantics) == nullptr);
    CHECK(cache.occupiedLevelsForFormula(semantics) == std::vector<int>{8, 10});

    const auto *authority = cache.findNearestUniformAncestorOrSelf(candidate, semantics);
    REQUIRE(authority != nullptr);
    CHECK(authority->key == uniformAuthority);
    CHECK(authority->valueState == gx::TileValueState::UniformTrue);
}
