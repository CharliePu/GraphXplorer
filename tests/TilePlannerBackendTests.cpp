#include "catch.hpp"

#include <algorithm>
#include <array>

#include "../src/Compute/ComputeBackend.h"
#include "../src/Compute/TilePlanner.h"
#include "../src/Tile/TileCache.h"

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
                .interval = Interval{1.0, 1.0}
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = generation},
                .semanticsHash = semantics,
                .key = key,
                .stage = gx::TileStage::UniformTrue,
                .classification = gx::TileClassification::UniformTrue,
                .interval = Interval{1.0, 1.0}
            }
        }
    };
}
}

TEST_CASE("TilePlanner emits dependency-ordered visible tile jobs", "[TilePlanner]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x+y>0");
    REQUIRE(formula.diagnostics.ok);

    const gx::ViewportRequest request{
        .header = {.requestId = 1, .generation = 1},
        .formula = formula.handle,
        .xRange = Interval{-1.0, 1.0},
        .yRange = Interval{-1.0, 1.0},
        .framebufferWidth = 512,
        .framebufferHeight = 512,
        .devicePixelRatio = 1.0
    };

    gx::TileCache cache;
    const auto jobs = gx::TilePlanner{}.plan(request, cache, gx::TilePlanBudget{}).jobs;
    REQUIRE_FALSE(jobs.empty());
    CHECK(jobs.front().kind == gx::JobKind::ClassifyInterval);
    CHECK(jobs.front().workClass == gx::WorkClass::VisibleNow);
}

TEST_CASE("TilePlanner seeds viewport from smallest sparse cover", "[TilePlanner]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x>y");
    REQUIRE(formula.diagnostics.ok);

    const gx::ViewportRequest request{
        .header = {.requestId = 1, .generation = 1},
        .formula = formula.handle,
        .xRange = Interval{-10.0, 10.0},
        .yRange = Interval{-10.0, 10.0},
        .framebufferWidth = 800,
        .framebufferHeight = 800,
        .devicePixelRatio = 1.0
    };

    CHECK(gx::seedTileLevelForViewport(request) == 4);

    gx::TileCache cache;
    const auto jobs = gx::TilePlanner{}.plan(request, cache, gx::TilePlanBudget{}).jobs;
    REQUIRE(jobs.size() == 4);
    CHECK(std::ranges::all_of(jobs, [](const gx::TileJob &job)
    {
        return job.kind == gx::JobKind::ClassifyInterval && job.key.level == 4;
    }));
}

TEST_CASE("TilePlanner skips descendants dominated by larger uniform authority", "[TilePlanner]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x=x");
    REQUIRE(formula.diagnostics.ok);

    const gx::ViewportRequest request{
        .header = {.requestId = 1, .generation = 1},
        .formula = formula.handle,
        .xRange = Interval{-10.0, -1.0},
        .yRange = Interval{-10.0, -1.0},
        .framebufferWidth = 800,
        .framebufferHeight = 800,
        .devicePixelRatio = 1.0
    };

    gx::TileCache cache;
    const gx::TileKey authority{-1, -1, 8};
    gx::TileTransaction tx{
        .header = {.requestId = 1, .generation = 1},
        .semanticsHash = formula.handle.semanticsHash,
        .deltas = {
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = formula.handle.semanticsHash,
                .key = authority,
                .stage = gx::TileStage::IntervalQueued,
                .classification = gx::TileClassification::Unknown
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = formula.handle.semanticsHash,
                .key = authority,
                .stage = gx::TileStage::IntervalReady,
                .classification = gx::TileClassification::UniformTrue,
                .interval = Interval{1.0, 1.0}
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = formula.handle.semanticsHash,
                .key = authority,
                .stage = gx::TileStage::UniformTrue,
                .classification = gx::TileClassification::UniformTrue,
                .interval = Interval{1.0, 1.0}
            }
        }
    };
    REQUIRE(cache.apply(tx).rejected == 0);

    const auto jobs = gx::TilePlanner{}.plan(request, cache, gx::TilePlanBudget{}).jobs;
    CHECK(jobs.empty());
}

TEST_CASE("TilePlanner emits one uniform authority instead of fragmented seed cells", "[TilePlanner]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x=x");
    REQUIRE(formula.diagnostics.ok);

    const gx::ViewportRequest request{
        .header = {.requestId = 1, .generation = 1},
        .formula = formula.handle,
        .xRange = Interval{-10.0, -1.0},
        .yRange = Interval{-10.0, -1.0},
        .framebufferWidth = 800,
        .framebufferHeight = 800,
        .devicePixelRatio = 1.0
    };

    gx::TileCache cache;
    const gx::TileKey authority{-1, -1, 8};
    gx::TileTransaction tx{
        .header = {.requestId = 1, .generation = 1},
        .semanticsHash = formula.handle.semanticsHash,
        .deltas = {
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = formula.handle.semanticsHash,
                .key = authority,
                .stage = gx::TileStage::IntervalQueued,
                .classification = gx::TileClassification::Unknown
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = formula.handle.semanticsHash,
                .key = authority,
                .stage = gx::TileStage::IntervalReady,
                .classification = gx::TileClassification::UniformTrue,
                .interval = Interval{1.0, 1.0}
            },
            gx::TileDelta{
                .header = {.requestId = 1, .generation = 1},
                .semanticsHash = formula.handle.semanticsHash,
                .key = authority,
                .stage = gx::TileStage::UniformTrue,
                .classification = gx::TileClassification::UniformTrue,
                .interval = Interval{1.0, 1.0}
            }
        }
    };
    REQUIRE(cache.apply(tx).rejected == 0);

    const auto plan = gx::TilePlanner{}.plan(request, cache, gx::TilePlanBudget{});
    CHECK(plan.jobs.empty());
    const auto *record = cache.find(authority, formula.handle.semanticsHash);
    REQUIRE(record != nullptr);
    CHECK(record->valueState == gx::TileValueState::UniformTrue);
}

TEST_CASE("TilePlanner produces work from the sparse traversal", "[TilePlanner]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x>y");
    REQUIRE(formula.diagnostics.ok);

    const gx::ViewportRequest request{
        .header = {.requestId = 2, .generation = 1},
        .formula = formula.handle,
        .xRange = Interval{-10.0, 10.0},
        .yRange = Interval{-10.0, 10.0},
        .framebufferWidth = 800,
        .framebufferHeight = 800,
        .devicePixelRatio = 1.0
    };

    gx::TileCache cache;
    const auto plan = gx::TilePlanner{}.plan(request, cache, gx::TilePlanBudget{});

    REQUIRE(plan.jobs.size() == 4);
    CHECK(std::ranges::all_of(plan.jobs, [](const gx::TileJob &job)
    {
        return job.kind == gx::JobKind::ClassifyInterval && job.key.level == 4;
    }));
    for (const auto &job : plan.jobs)
    {
        const auto *record = cache.find(job.key, formula.handle.semanticsHash);
        REQUIRE(record != nullptr);
        CHECK(record->workState == gx::TileWorkState::IntervalQueued);
    }
}

TEST_CASE("TilePlanner never schedules cached descendants below the viewport leaf level", "[TilePlanner]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x>y");
    REQUIRE(formula.diagnostics.ok);

    const gx::ViewportRequest request{
        .header = {.requestId = 3, .generation = 5},
        .formula = formula.handle,
        .xRange = Interval{-1024.0, 1024.0},
        .yRange = Interval{-1024.0, 1024.0},
        .framebufferWidth = 800,
        .framebufferHeight = 800,
        .devicePixelRatio = 1.0
    };
    const auto leafLevel = gx::leafTileLevel(request);
    REQUIRE(leafLevel == 9);

    gx::TileCache cache;
    const gx::TileKey staleFineTile{-4, -4, leafLevel - 1};
    auto result = cache.apply(uniformTrueTransaction(formula.handle.semanticsHash, staleFineTile, 1));
    REQUIRE(result.rejected == 0);

    const auto plan = gx::TilePlanner{}.plan(request, cache, gx::TilePlanBudget{});
    CHECK(std::ranges::none_of(plan.jobs, [leafLevel](const gx::TileJob &job)
    {
        return job.key.level < leafLevel;
    }));
    CHECK(std::ranges::any_of(plan.jobs, [leafLevel](const gx::TileJob &job)
    {
        return job.kind == gx::JobKind::ClassifyInterval && job.key.level >= leafLevel;
    }));
    CHECK(std::ranges::none_of(plan.jobs, [staleFineTile](const gx::TileJob &job)
    {
        return job.key == staleFineTile;
    }));
}

TEST_CASE("TilePlanner creates seed authority even when cached descendants exist", "[TilePlanner]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x>y");
    REQUIRE(formula.diagnostics.ok);

    const gx::ViewportRequest request{
        .header = {.requestId = 4, .generation = 6},
        .formula = formula.handle,
        .xRange = Interval{-1024.0, 1024.0},
        .yRange = Interval{-1024.0, 1024.0},
        .framebufferWidth = 800,
        .framebufferHeight = 800,
        .devicePixelRatio = 1.0
    };
    const auto seedLevel = gx::seedTileLevelForViewport(request);
    REQUIRE(seedLevel == 10);

    gx::TileCache cache;
    const gx::TileKey seed{-1, -1, seedLevel};
    for (const auto &child : gx::tileChildren(seed))
    {
        auto result = cache.apply(mixedRegionTransaction(formula.handle.semanticsHash, child, 1));
        REQUIRE(result.rejected == 0);
    }

    const auto plan = gx::TilePlanner{}.plan(request, cache, gx::TilePlanBudget{});
    CHECK(std::ranges::any_of(plan.jobs, [seedLevel](const gx::TileJob &job)
    {
        return job.kind == gx::JobKind::ClassifyInterval && job.key.level == seedLevel;
    }));
    CHECK(std::ranges::any_of(cache.recordsForFormula(formula.handle.semanticsHash), [seedLevel](const gx::TileRecord &record)
    {
        return record.key.level == seedLevel && record.workState == gx::TileWorkState::IntervalQueued;
    }));
    for (const auto &child : gx::tileChildren(seed))
    {
        CHECK(cache.find(child, formula.handle.semanticsHash) != nullptr);
    }
}

TEST_CASE("CpuComputeBackend classifies interval batches without renderer dependencies", "[ComputeBackend]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x+y>0");
    REQUIRE(formula.diagnostics.ok);

    std::array keys{gx::TileKey{0, 0, 0}};
    std::array xMin{-1.0};
    std::array xMax{1.0};
    std::array yMin{-1.0};
    std::array yMax{1.0};
    std::array<gx::TileClassificationResult, 1> out{};

    gx::CpuComputeBackend backend;
    const auto result = backend.classifyIntervals(
        gx::IntervalBatchView{&formula, keys, xMin, xMax, yMin, yMax},
        out);

    CHECK(result.ok);
    CHECK(result.completed == 1);
    CHECK(out.front().classification == gx::TileClassification::Mixed);
}
