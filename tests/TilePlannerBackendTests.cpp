#include "catch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>

#include "../src/Compute/ComputeBackend.h"
#include "../src/Compute/InequalityTileRefiner.h"
#include "../src/Compute/TilePlanner.h"
#include "../src/Tile/TileCache.h"
#include "../src/Util/ThreadPool.h"

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

bool containsStrictPeriodicPoint(const double lower,
                                 const double upper,
                                 const double phase,
                                 const double period)
{
    const auto k = std::ceil((lower - phase) / period);
    const auto point = phase + k * period;
    return point > lower && point < upper;
}

bool containsTangentPole(const double lower, const double upper)
{
    return containsStrictPeriodicPoint(lower, upper, std::numbers::pi / 2.0, std::numbers::pi);
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

TEST_CASE("TilePlanner parallel discovery commits the same work as sequential discovery", "[TilePlanner]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x>y");
    REQUIRE(formula.diagnostics.ok);

    const gx::ViewportRequest request{
        .header = {.requestId = 20, .generation = 2},
        .formula = formula.handle,
        .xRange = Interval{-10.0, 10.0},
        .yRange = Interval{-10.0, 10.0},
        .framebufferWidth = 800,
        .framebufferHeight = 800,
        .devicePixelRatio = 1.0
    };
    constexpr auto refinementDepth = 2;
    const auto seedLevel = gx::seedTileLevelForViewport(request);
    const gx::TileKey mixedSeed{-1, -1, seedLevel};

    auto makeCache = [&]()
    {
        gx::TileCache cache;
        const auto result = cache.apply(mixedRegionTransaction(
            formula.handle.semanticsHash,
            mixedSeed,
            1));
        REQUIRE(result.rejected == 0);
        return cache;
    };

    auto sequentialCache = makeCache();
    auto parallelCache = makeCache();
    ThreadPool workers{2};

    const auto budget = gx::TilePlanBudget{
        .maxIntervalJobsPerFrame = 64,
        .maxRasterJobsPerFrame = 64
    };
    const auto sequentialPlan = gx::TilePlanner{}.plan(
        request,
        sequentialCache,
        budget,
        4,
        refinementDepth);
    const auto parallelPlan = gx::TilePlanner{}.plan(
        request,
        parallelCache,
        budget,
        4,
        refinementDepth,
        &workers);

    REQUIRE(parallelPlan.jobs.size() == sequentialPlan.jobs.size());
    for (size_t index = 0; index < parallelPlan.jobs.size(); ++index)
    {
        const auto &parallelJob = parallelPlan.jobs[index];
        const auto &sequentialJob = sequentialPlan.jobs[index];
        CHECK(parallelJob.kind == sequentialJob.kind);
        CHECK(parallelJob.workClass == sequentialJob.workClass);
        CHECK(parallelJob.key == sequentialJob.key);
        CHECK(parallelJob.priority == sequentialJob.priority);
        CHECK(parallelJob.dependencies == sequentialJob.dependencies);
        CHECK(parallelJob.interval.has_value() == sequentialJob.interval.has_value());
        if (parallelJob.interval && sequentialJob.interval)
        {
            CHECK(parallelJob.interval->lower == sequentialJob.interval->lower);
            CHECK(parallelJob.interval->upper == sequentialJob.interval->upper);
        }
    }
    CHECK(parallelPlan.erasedShadowedTiles == sequentialPlan.erasedShadowedTiles);
    for (const auto &job : parallelPlan.jobs)
    {
        const auto *sequentialRecord = sequentialCache.find(job.key, formula.handle.semanticsHash);
        const auto *parallelRecord = parallelCache.find(job.key, formula.handle.semanticsHash);
        REQUIRE(sequentialRecord != nullptr);
        REQUIRE(parallelRecord != nullptr);
        CHECK(parallelRecord->valueState == sequentialRecord->valueState);
        CHECK(parallelRecord->workState == sequentialRecord->workState);
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
    constexpr auto refinementDepth = 1;
    const auto seedLevel = gx::seedTileLevelForViewport(request);
    const auto leafLevel = gx::leafTileLevelForSeed(seedLevel, refinementDepth);
    REQUIRE(seedLevel == 10);
    REQUIRE(leafLevel == 9);

    gx::TileCache cache;
    const gx::TileKey staleFineTile{-4, -4, leafLevel - 1};
    auto result = cache.apply(uniformTrueTransaction(formula.handle.semanticsHash, staleFineTile, 1));
    REQUIRE(result.rejected == 0);

    const auto plan = gx::TilePlanner{}.plan(request, cache, gx::TilePlanBudget{}, 4, refinementDepth);
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

TEST_CASE("TilePlanner schedules huge zoom-out viewports above the old fixed level cap", "[TilePlanner]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x>y");
    REQUIRE(formula.diagnostics.ok);

    const gx::ViewportRequest request{
        .header = {.requestId = 5, .generation = 7},
        .formula = formula.handle,
        .xRange = Interval{-1.0e12, 1.0e12},
        .yRange = Interval{-1.0e12, 1.0e12},
        .framebufferWidth = 1600,
        .framebufferHeight = 1000,
        .devicePixelRatio = 1.0
    };
    const auto seedLevel = gx::seedTileLevelForViewport(request);
    REQUIRE(seedLevel > 30);

    gx::TileCache cache;
    const auto plan = gx::TilePlanner{}.plan(request, cache, gx::TilePlanBudget{});

    REQUIRE_FALSE(plan.jobs.empty());
    CHECK(std::ranges::all_of(plan.jobs, [seedLevel](const gx::TileJob &job)
    {
        return job.kind == gx::JobKind::ClassifyInterval && job.key.level == seedLevel;
    }));
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

TEST_CASE("CpuComputeBackend proves subpixel existence when interval subdivision succeeds", "[ComputeBackend]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x<=y");
    REQUIRE(formula.diagnostics.ok);

    std::array keys{gx::TileKey{0, 0, 0}};
    std::array xMin{0.0};
    std::array xMax{1.0};
    std::array yMin{0.0};
    std::array yMax{1.0};
    std::array offsets{uint32_t{0}};
    std::array<gx::RegionOutput, 1> out{};

    gx::CpuComputeBackend backend;
    const auto result = backend.rasterizeRegions(
        gx::RasterBatchView{&formula, keys, xMin, xMax, yMin, yMax, offsets, 1},
        out);

    REQUIRE(result.ok);
    REQUIRE(out.front().pixels.size() == 1);
    CHECK(static_cast<int>(out.front().pixels.front()) == 255);
    CHECK(out.front().certainty == gx::TextureCertainty::Precise);
}

TEST_CASE("CpuComputeBackend marks fully interval-proven raster output precise", "[ComputeBackend]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x<2");
    REQUIRE(formula.diagnostics.ok);

    std::array keys{gx::TileKey{0, 0, 0}};
    std::array xMin{0.0};
    std::array xMax{1.0};
    std::array yMin{0.0};
    std::array yMax{1.0};
    std::array offsets{uint32_t{0}};
    std::array<gx::RegionOutput, 1> out{};

    gx::CpuComputeBackend backend;
    const auto result = backend.rasterizeRegions(
        gx::RasterBatchView{&formula, keys, xMin, xMax, yMin, yMax, offsets, 1},
        out);

    REQUIRE(result.ok);
    REQUIRE(out.front().pixels.size() == 1);
    CHECK(static_cast<int>(out.front().pixels.front()) == 255);
    CHECK(out.front().certainty == gx::TextureCertainty::Precise);
}

TEST_CASE("CpuComputeBackend proves threshold pixels precise through subpixel refinement", "[ComputeBackend]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x>4");
    REQUIRE(formula.diagnostics.ok);

    std::array keys{gx::TileKey{0, 0, 0}};
    std::array xMin{3.0};
    std::array xMax{5.0};
    std::array yMin{0.0};
    std::array yMax{1.0};
    std::array offsets{uint32_t{0}};
    std::array<gx::RegionOutput, 1> out{};

    gx::CpuComputeBackend backend;
    const auto result = backend.rasterizeRegions(
        gx::RasterBatchView{&formula, keys, xMin, xMax, yMin, yMax, offsets, 1},
        out);

    REQUIRE(result.ok);
    REQUIRE(out.front().pixels.size() == 1);
    CHECK(static_cast<int>(out.front().pixels.front()) == 255);
    CHECK(out.front().certainty == gx::TextureCertainty::Precise);
}

TEST_CASE("CpuComputeBackend rasterizer proves inequality pixels across tan poles", "[ComputeBackend]")
{
    const auto formula = gx::FormulaCompiler{}.compile("y<=tan(x*y)");
    REQUIRE(formula.diagnostics.ok);

    std::array keys{gx::TileKey{0, 0, 0}};
    std::array xMin{1.5};
    std::array xMax{1.6};
    std::array yMin{1.0};
    std::array yMax{1.0};
    std::array offsets{uint32_t{0}};
    std::array<gx::RegionOutput, 1> out{};

    gx::CpuComputeBackend backend;
    const auto result = backend.rasterizeRegions(
        gx::RasterBatchView{&formula, keys, xMin, xMax, yMin, yMax, offsets, 1},
        out);

    REQUIRE(result.ok);
    REQUIRE(out.front().pixels.size() == 1);
    CHECK(static_cast<int>(out.front().pixels.front()) == 255);
    CHECK(out.front().certainty == gx::TextureCertainty::Precise);
}

TEST_CASE("Default ComputeBackend does not CPU-render GPU preview fallback",
          "[ComputeBackend]")
{
    auto backend = gx::makeDefaultComputeBackend();
    REQUIRE(backend);

    const auto formula = gx::FormulaCompiler{}.compile("y<=tan(x*y)");
    REQUIRE(formula.diagnostics.ok);
    std::array keys{gx::TileKey{0, 0, 0}};
    std::array xMin{1.5};
    std::array xMax{1.6};
    std::array yMin{1.0};
    std::array yMax{1.0};
    std::array offsets{uint32_t{0}};
    std::array<gx::RegionOutput, 1> out{};

    const auto result = backend->rasterizeRegions(
        gx::RasterBatchView{&formula, keys, xMin, xMax, yMin, yMax, offsets, 1},
        out);

    CHECK_FALSE(result.ok);
    CHECK(result.completed == 0);
    CHECK(result.message.find("GPU preview unavailable") != std::string::npos);
}

TEST_CASE("Default ComputeBackend runs CPU refinement only when GPU is disabled",
          "[ComputeBackend]")
{
    auto backend = gx::makeDefaultComputeBackend();
    REQUIRE(backend);

    const auto formula = gx::FormulaCompiler{}.compile("y<=tan(x*y)");
    REQUIRE(formula.diagnostics.ok);
    std::array keys{gx::TileKey{0, 0, 0}};
    std::array xMin{1.5};
    std::array xMax{1.6};
    std::array yMin{1.0};
    std::array yMax{1.0};
    std::array offsets{uint32_t{0}};
    std::array<gx::RegionOutput, 1> out{};

    const auto result = backend->rasterizeRegions(
        gx::RasterBatchView{
            &formula,
            keys,
            xMin,
            xMax,
            yMin,
            yMax,
            offsets,
            1,
            gx::TexturePreparationMode::Refined
        },
        out);

    REQUIRE(result.ok);
    REQUIRE(out.front().pixels.size() == 1);
    CHECK(static_cast<int>(out.front().pixels.front()) == 255);
    CHECK(out.front().certainty == gx::TextureCertainty::Precise);
}

TEST_CASE("CpuComputeBackend marks exhausted inequality pixels best-estimate", "[ComputeBackend]")
{
    const auto formula = gx::FormulaCompiler{}.compile("y<log(x)");
    REQUIRE(formula.diagnostics.ok);

    std::array keys{gx::TileKey{0, 0, 0}};
    std::array xMin{-2.0};
    std::array xMax{-1.0};
    std::array yMin{0.0};
    std::array yMax{1.0};
    std::array offsets{uint32_t{0}};
    std::array<gx::RegionOutput, 1> out{};

    gx::CpuComputeBackend backend;
    const auto result = backend.rasterizeRegions(
        gx::RasterBatchView{&formula, keys, xMin, xMax, yMin, yMax, offsets, 1},
        out);

    REQUIRE(result.ok);
    REQUIRE(out.front().pixels.size() == 1);
    CHECK(static_cast<int>(out.front().pixels.front()) == 127);
    CHECK(out.front().certainty == gx::TextureCertainty::BestEstimate);
}

TEST_CASE("InequalityTileRefiner rasterizes affine inequalities exactly", "[ComputeBackend]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x<=y");
    REQUIRE(formula.diagnostics.ok);
    REQUIRE(formula.affineInequality.has_value());

    const auto result = gx::refineInequalityTile(
        formula,
        gx::TileKey{0, 0, 0},
        gx::InequalityTileRefinementOptions{
            .pixelsPerAxis = 16,
            .rootBounds = gx::Rect{0.0, 1.0, 0.0, 1.0}
        });

    REQUIRE(result.ok);
    CHECK(result.visitedNodes == 1);
    CHECK(result.unknownPixels == 0);
    CHECK(result.region.certainty == gx::TextureCertainty::Precise);
    CHECK(std::ranges::none_of(result.region.pixels, [](const uint8_t pixel)
    {
        return pixel == uint8_t{127};
    }));
    CHECK(std::ranges::any_of(result.region.pixels, [](const uint8_t pixel)
    {
        return pixel == uint8_t{0};
    }));
    CHECK(std::ranges::any_of(result.region.pixels, [](const uint8_t pixel)
    {
        return pixel == uint8_t{255};
    }));
}

TEST_CASE("InequalityTileRefiner prunes empty-domain comparison branches", "[ComputeBackend]")
{
    const auto formula = gx::FormulaCompiler{}.compile("y<log(x)");
    REQUIRE(formula.diagnostics.ok);

    const auto result = gx::refineInequalityTile(
        formula,
        gx::TileKey{0, 0, 0},
        gx::InequalityTileRefinementOptions{
            .pixelsPerAxis = 16,
            .rootBounds = gx::Rect{-2.0, -1.0, 0.0, 1.0}
        });

    REQUIRE(result.ok);
    CHECK(result.visitedNodes == 1);
    CHECK(result.region.certainty == gx::TextureCertainty::BestEstimate);
    CHECK(std::ranges::all_of(result.region.pixels, [](const uint8_t pixel)
    {
        return pixel == uint8_t{127};
    }));
}

TEST_CASE("InequalityTileRefiner proves tangent pole pixels in one pass", "[ComputeBackend]")
{
    const auto formula = gx::FormulaCompiler{}.compile("tan(x)>y");
    REQUIRE(formula.diagnostics.ok);
    REQUIRE(formula.tangentPoleInequality.has_value());

    const auto result = gx::refineInequalityTile(
        formula,
        gx::TileKey{0, 0, 0},
        gx::InequalityTileRefinementOptions{
            .pixelsPerAxis = 16,
            .rootBounds = gx::Rect{-1000.0, 1000.0, -1.0, 1.0}
        });

    REQUIRE(result.ok);
    CHECK(result.visitedNodes == 1);
    CHECK(result.unknownPixels == 0);
    CHECK(result.region.certainty == gx::TextureCertainty::Precise);
    CHECK(std::ranges::all_of(result.region.pixels, [](const uint8_t pixel)
    {
        return pixel == uint8_t{255};
    }));
}

TEST_CASE("InequalityTileRefiner rasterizes tangent-axis curves without recursive budget fallback", "[ComputeBackend]")
{
    constexpr auto pixels = uint32_t{256};
    constexpr auto bounds = gx::Rect{-80.0, 80.0, 0.0, 20.0};
    const auto formula = gx::FormulaCompiler{}.compile("tan(x)>y");
    const auto flippedFormula = gx::FormulaCompiler{}.compile("y<tan(x)");
    REQUIRE(formula.diagnostics.ok);
    REQUIRE(flippedFormula.diagnostics.ok);

    const auto options = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = pixels,
        .rootBounds = bounds
    };
    const auto result = gx::refineInequalityTile(formula, gx::TileKey{0, 0, 0}, options);
    const auto flippedResult = gx::refineInequalityTile(flippedFormula, gx::TileKey{0, 0, 0}, options);

    REQUIRE(result.ok);
    REQUIRE(flippedResult.ok);
    CHECK(result.region.pixels == flippedResult.region.pixels);
    CHECK(result.visitedNodes == 1);
    CHECK(result.unknownPixels == 0);
    CHECK(result.region.certainty == gx::TextureCertainty::Precise);

    const auto xStep = (bounds.xMax - bounds.xMin) / static_cast<double>(pixels);
    const auto yStep = (bounds.yMax - bounds.yMin) / static_cast<double>(pixels);
    auto truePixels = size_t{0};
    auto falsePixels = size_t{0};
    for (uint32_t y = 0; y < pixels; ++y)
    {
        const auto yMin = bounds.yMin + static_cast<double>(y) * yStep;
        for (uint32_t x = 0; x < pixels; ++x)
        {
            const auto xMin = bounds.xMin + static_cast<double>(x) * xStep;
            const auto xMax = x == pixels - 1 ? bounds.xMax : xMin + xStep;
            auto expected = true;
            if (!containsTangentPole(xMin, xMax))
            {
                const auto xTanMin = std::tan(xMin);
                const auto xTanMax = std::tan(xMax);
                expected = std::max(xTanMin, xTanMax) > yMin;
            }

            const auto pixel = result.region.pixels[static_cast<size_t>(y) * pixels + x];
            CHECK(pixel == (expected ? uint8_t{255} : uint8_t{0}));
            truePixels += pixel == uint8_t{255} ? 1u : 0u;
            falsePixels += pixel == uint8_t{0} ? 1u : 0u;
        }
    }
    CHECK(truePixels > 0);
    CHECK(falsePixels > 0);
}

TEST_CASE("InequalityTileRefiner records compact proof trees by default", "[ComputeBackend]")
{
    const auto formula = gx::FormulaCompiler{}.compile("sin(x*y)<sin(sin(y))");
    REQUIRE(formula.diagnostics.ok);

    const auto summary = gx::refineInequalityTile(
        formula,
        gx::TileKey{0, 0, 0},
        gx::InequalityTileRefinementOptions{
            .pixelsPerAxis = 16,
            .rootBounds = gx::Rect{-8.0, 8.0, -8.0, 8.0}
        });
    REQUIRE(summary.ok);
    REQUIRE_FALSE(summary.region.proofTree.nodes.empty());
    CHECK(summary.region.proofTree.nodes.size() == 1);
    CHECK(summary.region.proofTree.nodes.front().key == gx::TileKey{0, 0, 0});

    const auto detailed = gx::refineInequalityTile(
        formula,
        gx::TileKey{0, 0, 0},
        gx::InequalityTileRefinementOptions{
            .pixelsPerAxis = 16,
            .rootBounds = gx::Rect{-8.0, 8.0, -8.0, 8.0},
            .recordDetailedProofTree = true
        });
    REQUIRE(detailed.ok);
    CHECK(detailed.region.pixels == summary.region.pixels);
    CHECK(detailed.region.proofTree.nodes.size() > summary.region.proofTree.nodes.size());
}

TEST_CASE("InequalityTileRefiner large compact split preserves sequential output", "[ComputeBackend]")
{
    const auto formula = gx::FormulaCompiler{}.compile("sin(20*x*y)<sin(sin(10*y))");
    REQUIRE(formula.diagnostics.ok);

    const auto thresholdCompact = gx::refineInequalityTile(
        formula,
        gx::TileKey{0, 0, 0},
        gx::InequalityTileRefinementOptions{
            .pixelsPerAxis = 128,
            .rootBounds = gx::Rect{-2.0, 2.0, -2.0, 2.0}
        });
    const auto thresholdSequential = gx::refineInequalityTile(
        formula,
        gx::TileKey{0, 0, 0},
        gx::InequalityTileRefinementOptions{
            .pixelsPerAxis = 128,
            .rootBounds = gx::Rect{-2.0, 2.0, -2.0, 2.0},
            .recordDetailedProofTree = true
        });
    REQUIRE(thresholdCompact.ok);
    REQUIRE(thresholdSequential.ok);
    CHECK(thresholdCompact.region.pixels == thresholdSequential.region.pixels);
    CHECK(thresholdCompact.region.certainty == thresholdSequential.region.certainty);
    CHECK(thresholdCompact.region.existence == thresholdSequential.region.existence);
    CHECK(thresholdCompact.unknownPixels == thresholdSequential.unknownPixels);

    const auto compact = gx::refineInequalityTile(
        formula,
        gx::TileKey{0, 0, 0},
        gx::InequalityTileRefinementOptions{
            .pixelsPerAxis = 1024,
            .rootBounds = gx::Rect{-2.0, 2.0, -2.0, 2.0}
        });
    const auto sequential = gx::refineInequalityTile(
        formula,
        gx::TileKey{0, 0, 0},
        gx::InequalityTileRefinementOptions{
            .pixelsPerAxis = 1024,
            .rootBounds = gx::Rect{-2.0, 2.0, -2.0, 2.0},
            .recordDetailedProofTree = true
        });

    REQUIRE(compact.ok);
    REQUIRE(sequential.ok);
    CHECK(compact.region.pixels == sequential.region.pixels);
    CHECK(compact.region.certainty == sequential.region.certainty);
    CHECK(compact.region.existence == sequential.region.existence);
    CHECK(compact.unknownPixels == sequential.unknownPixels);
    CHECK(compact.region.proofTree.nodes.size() == 1);
    CHECK(sequential.region.proofTree.nodes.size() > compact.region.proofTree.nodes.size());
}

TEST_CASE("InequalityTileRefiner preserves output with axis-only root operand cache", "[ComputeBackend]")
{
    const auto nestedCached = gx::FormulaCompiler{}.compile("sin(x*y)<sin(sin(y))");
    const auto nestedUncached = gx::FormulaCompiler{}.compile("sin(x*y)<sin(sin(y+0*x))");
    REQUIRE(nestedCached.diagnostics.ok);
    REQUIRE(nestedUncached.diagnostics.ok);
    REQUIRE(nestedCached.rootComparisonBytecode.has_value());
    REQUIRE(nestedUncached.rootComparisonBytecode.has_value());
    CHECK(nestedCached.rootComparisonBytecode->rhs.variableMask
        != nestedUncached.rootComparisonBytecode->rhs.variableMask);

    const auto nestedOptions = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = 32,
        .rootBounds = gx::Rect{-8.0, 8.0, -8.0, 8.0}
    };
    const auto nestedCachedResult = gx::refineInequalityTile(nestedCached, gx::TileKey{0, 0, 0}, nestedOptions);
    const auto nestedUncachedResult = gx::refineInequalityTile(
        nestedUncached,
        gx::TileKey{0, 0, 0},
        nestedOptions);

    REQUIRE(nestedCachedResult.ok);
    REQUIRE(nestedUncachedResult.ok);
    CHECK(nestedCachedResult.region.pixels == nestedUncachedResult.region.pixels);
    CHECK(nestedCachedResult.region.certainty == nestedUncachedResult.region.certainty);
    CHECK(nestedCachedResult.region.existence == nestedUncachedResult.region.existence);

    const auto logCached = gx::FormulaCompiler{}.compile("y<log(x)");
    const auto logUncached = gx::FormulaCompiler{}.compile("y<log(x+0*y)");
    REQUIRE(logCached.diagnostics.ok);
    REQUIRE(logUncached.diagnostics.ok);
    REQUIRE(logCached.rootComparisonBytecode.has_value());
    REQUIRE(logUncached.rootComparisonBytecode.has_value());
    CHECK(logCached.rootComparisonBytecode->rhs.variableMask != logUncached.rootComparisonBytecode->rhs.variableMask);

    const auto logOptions = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = 32,
        .rootBounds = gx::Rect{-2.0, 2.0, -2.0, 2.0}
    };
    const auto logCachedResult = gx::refineInequalityTile(logCached, gx::TileKey{0, 0, 0}, logOptions);
    const auto logUncachedResult = gx::refineInequalityTile(logUncached, gx::TileKey{0, 0, 0}, logOptions);

    REQUIRE(logCachedResult.ok);
    REQUIRE(logUncachedResult.ok);
    CHECK(logCachedResult.region.pixels == logUncachedResult.region.pixels);
    CHECK(logCachedResult.region.certainty == logUncachedResult.region.certainty);
    CHECK(logCachedResult.region.existence == logUncachedResult.region.existence);
}

TEST_CASE("InequalityTileRefiner rasterizes log-domain curve rows in one pass", "[ComputeBackend]")
{
    const auto logDirect = gx::FormulaCompiler{}.compile("y<log(x)");
    const auto logGeneric = gx::FormulaCompiler{}.compile("y<log(x+0*y)");
    REQUIRE(logDirect.diagnostics.ok);
    REQUIRE(logGeneric.diagnostics.ok);

    const auto options = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = 128,
        .rootBounds = gx::Rect{-2.0, 2.0, -2.0, 2.0}
    };
    const auto directResult = gx::refineInequalityTile(logDirect, gx::TileKey{0, 0, 0}, options);
    const auto genericResult = gx::refineInequalityTile(logGeneric, gx::TileKey{0, 0, 0}, options);

    REQUIRE(directResult.ok);
    REQUIRE(genericResult.ok);
    CHECK(directResult.visitedNodes == 1);
    CHECK(genericResult.visitedNodes > directResult.visitedNodes);
    CHECK(directResult.region.pixels == genericResult.region.pixels);
    CHECK(directResult.region.certainty == genericResult.region.certainty);
    CHECK(directResult.region.existence == genericResult.region.existence);
}

TEST_CASE("InequalityTileRefiner rasterizes exp curve rows in one pass", "[ComputeBackend]")
{
    const auto expDirect = gx::FormulaCompiler{}.compile("exp(x)-y>0");
    const auto expGeneric = gx::FormulaCompiler{}.compile("exp(x+0*y)-y>0");
    REQUIRE(expDirect.diagnostics.ok);
    REQUIRE(expGeneric.diagnostics.ok);

    const auto options = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = 128,
        .rootBounds = gx::Rect{-4.0, 4.0, 0.0, 20.0}
    };
    const auto directResult = gx::refineInequalityTile(expDirect, gx::TileKey{0, 0, 0}, options);
    const auto genericResult = gx::refineInequalityTile(expGeneric, gx::TileKey{0, 0, 0}, options);

    REQUIRE(directResult.ok);
    REQUIRE(genericResult.ok);
    CHECK(directResult.visitedNodes == 1);
    CHECK(genericResult.visitedNodes > directResult.visitedNodes);
    CHECK(directResult.region.pixels == genericResult.region.pixels);
    CHECK(directResult.region.certainty == genericResult.region.certainty);
    CHECK(directResult.region.existence == genericResult.region.existence);
}

TEST_CASE("InequalityTileRefiner rasterizes sqrt curve rows with domain-boundary fallback", "[ComputeBackend]")
{
    const auto sqrtDirect = gx::FormulaCompiler{}.compile("y<sqrt(x)");
    const auto sqrtGeneric = gx::FormulaCompiler{}.compile("y<sqrt(x+0*y)");
    REQUIRE(sqrtDirect.diagnostics.ok);
    REQUIRE(sqrtGeneric.diagnostics.ok);

    const auto options = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = 128,
        .rootBounds = gx::Rect{-1.0, 4.0, -1.0, 3.0}
    };
    const auto directResult = gx::refineInequalityTile(sqrtDirect, gx::TileKey{0, 0, 0}, options);
    const auto genericResult = gx::refineInequalityTile(sqrtGeneric, gx::TileKey{0, 0, 0}, options);

    REQUIRE(directResult.ok);
    REQUIRE(genericResult.ok);
    CHECK(directResult.visitedNodes == 1);
    auto directUnknown = size_t{0};
    auto genericUnknown = size_t{0};
    auto knownPixelsMatch = true;
    REQUIRE(directResult.region.pixels.size() == genericResult.region.pixels.size());
    for (auto index = size_t{0}; index < directResult.region.pixels.size(); ++index)
    {
        directUnknown += directResult.region.pixels[index] == uint8_t{127} ? 1u : 0u;
        genericUnknown += genericResult.region.pixels[index] == uint8_t{127} ? 1u : 0u;
        if (genericResult.region.pixels[index] != uint8_t{127})
        {
            knownPixelsMatch = knownPixelsMatch && directResult.region.pixels[index] == genericResult.region.pixels[index];
        }
    }
    CHECK(knownPixelsMatch);
    CHECK(directUnknown < genericUnknown);
    CHECK(directResult.region.certainty == genericResult.region.certainty);
    CHECK(directResult.region.existence == genericResult.region.existence);
}

TEST_CASE("InequalityTileRefiner rasterizes sum-squares disk rows in one pass", "[ComputeBackend]")
{
    const auto circleDirect = gx::FormulaCompiler{}.compile("x^2+y^2<16");
    const auto circleGeneric = gx::FormulaCompiler{}.compile("x*x+y*y<16");
    REQUIRE(circleDirect.diagnostics.ok);
    REQUIRE(circleGeneric.diagnostics.ok);

    const auto options = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = 128,
        .rootBounds = gx::Rect{-5.0, 5.0, -5.0, 5.0}
    };
    const auto directResult = gx::refineInequalityTile(circleDirect, gx::TileKey{0, 0, 0}, options);
    const auto genericResult = gx::refineInequalityTile(circleGeneric, gx::TileKey{0, 0, 0}, options);

    REQUIRE(directResult.ok);
    REQUIRE(genericResult.ok);
    CHECK(directResult.visitedNodes == 1);
    CHECK(genericResult.visitedNodes > directResult.visitedNodes);
    CHECK(directResult.region.pixels == genericResult.region.pixels);
    CHECK(directResult.region.certainty == genericResult.region.certainty);
    CHECK(directResult.region.existence == genericResult.region.existence);
}

TEST_CASE("InequalityTileRefiner rasterizes sum-squares exterior rows in one pass", "[ComputeBackend]")
{
    const auto circleDirect = gx::FormulaCompiler{}.compile("x^2+y^2>16");
    const auto circleGeneric = gx::FormulaCompiler{}.compile("x*x+y*y>16");
    REQUIRE(circleDirect.diagnostics.ok);
    REQUIRE(circleGeneric.diagnostics.ok);

    const auto options = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = 128,
        .rootBounds = gx::Rect{-5.0, 5.0, -5.0, 5.0}
    };
    const auto directResult = gx::refineInequalityTile(circleDirect, gx::TileKey{0, 0, 0}, options);
    const auto genericResult = gx::refineInequalityTile(circleGeneric, gx::TileKey{0, 0, 0}, options);

    REQUIRE(directResult.ok);
    REQUIRE(genericResult.ok);
    CHECK(directResult.visitedNodes == 1);
    CHECK(genericResult.visitedNodes > directResult.visitedNodes);
    CHECK(directResult.region.pixels == genericResult.region.pixels);
    CHECK(directResult.region.certainty == genericResult.region.certainty);
    CHECK(directResult.region.existence == genericResult.region.existence);
}

TEST_CASE("InequalityTileRefiner rasterizes square-difference band rows in one pass", "[ComputeBackend]")
{
    const auto bandDirect = gx::FormulaCompiler{}.compile("(x-y)^2<0.0001");
    const auto bandGeneric = gx::FormulaCompiler{}.compile("(x-y)*(x-y)<0.0001");
    REQUIRE(bandDirect.diagnostics.ok);
    REQUIRE(bandGeneric.diagnostics.ok);

    const auto options = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = 128,
        .rootBounds = gx::Rect{-1.0, 1.0, -1.0, 1.0}
    };
    const auto directResult = gx::refineInequalityTile(bandDirect, gx::TileKey{0, 0, 0}, options);
    const auto genericResult = gx::refineInequalityTile(bandGeneric, gx::TileKey{0, 0, 0}, options);

    REQUIRE(directResult.ok);
    REQUIRE(genericResult.ok);
    CHECK(directResult.visitedNodes == 1);
    CHECK(genericResult.visitedNodes > directResult.visitedNodes);
    CHECK(directResult.region.pixels == genericResult.region.pixels);
    CHECK(directResult.region.certainty == genericResult.region.certainty);
    CHECK(directResult.region.existence == genericResult.region.existence);
}

TEST_CASE("InequalityTileRefiner rasterizes square-difference exterior rows in one pass", "[ComputeBackend]")
{
    const auto bandDirect = gx::FormulaCompiler{}.compile("(x-y)^2>0.0001");
    const auto bandGeneric = gx::FormulaCompiler{}.compile("(x-y)*(x-y)>0.0001");
    REQUIRE(bandDirect.diagnostics.ok);
    REQUIRE(bandGeneric.diagnostics.ok);

    const auto options = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = 128,
        .rootBounds = gx::Rect{-1.0, 1.0, -1.0, 1.0}
    };
    const auto directResult = gx::refineInequalityTile(bandDirect, gx::TileKey{0, 0, 0}, options);
    const auto genericResult = gx::refineInequalityTile(bandGeneric, gx::TileKey{0, 0, 0}, options);

    REQUIRE(directResult.ok);
    REQUIRE(genericResult.ok);
    CHECK(directResult.visitedNodes == 1);
    CHECK(genericResult.visitedNodes > directResult.visitedNodes);
    CHECK(directResult.region.pixels == genericResult.region.pixels);
    CHECK(directResult.region.certainty == genericResult.region.certainty);
    CHECK(directResult.region.existence == genericResult.region.existence);
}

TEST_CASE("InequalityTileRefiner rasterizes reciprocal asymptote rows in one pass", "[ComputeBackend]")
{
    const auto rationalDirect = gx::FormulaCompiler{}.compile("y<1/(x-0.001)");
    const auto rationalGeneric = gx::FormulaCompiler{}.compile("y<1/(x+0*y-0.001)");
    REQUIRE(rationalDirect.diagnostics.ok);
    REQUIRE(rationalGeneric.diagnostics.ok);

    const auto options = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = 128,
        .rootBounds = gx::Rect{-2.0, 2.0, -5.0, 5.0}
    };
    const auto directResult = gx::refineInequalityTile(rationalDirect, gx::TileKey{0, 0, 0}, options);
    const auto genericResult = gx::refineInequalityTile(rationalGeneric, gx::TileKey{0, 0, 0}, options);

    REQUIRE(directResult.ok);
    REQUIRE(genericResult.ok);
    CHECK(directResult.visitedNodes == 1);
    CHECK(genericResult.visitedNodes > directResult.visitedNodes);
    CHECK(directResult.region.pixels == genericResult.region.pixels);
    CHECK(directResult.region.certainty == genericResult.region.certainty);
    CHECK(directResult.region.existence == genericResult.region.existence);
}

TEST_CASE("InequalityTileRefiner preserves output with direct root comparison operands", "[ComputeBackend]")
{
    const auto circleDirect = gx::FormulaCompiler{}.compile("x^2+y^2<16");
    const auto circleGeneric = gx::FormulaCompiler{}.compile("x*x+y*y<16");
    REQUIRE(circleDirect.diagnostics.ok);
    REQUIRE(circleGeneric.diagnostics.ok);

    const auto circleOptions = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = 32,
        .rootBounds = gx::Rect{-5.0, 5.0, -5.0, 5.0}
    };
    const auto circleDirectResult = gx::refineInequalityTile(
        circleDirect,
        gx::TileKey{0, 0, 0},
        circleOptions);
    const auto circleGenericResult = gx::refineInequalityTile(
        circleGeneric,
        gx::TileKey{0, 0, 0},
        circleOptions);

    REQUIRE(circleDirectResult.ok);
    REQUIRE(circleGenericResult.ok);
    CHECK(circleDirectResult.region.pixels == circleGenericResult.region.pixels);
    CHECK(circleDirectResult.region.certainty == circleGenericResult.region.certainty);
    CHECK(circleDirectResult.region.existence == circleGenericResult.region.existence);

    const auto bandDirect = gx::FormulaCompiler{}.compile("(x-y)^2<0.0001");
    const auto bandGeneric = gx::FormulaCompiler{}.compile("(x-y)*(x-y)<0.0001");
    REQUIRE(bandDirect.diagnostics.ok);
    REQUIRE(bandGeneric.diagnostics.ok);

    const auto bandOptions = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = 32,
        .rootBounds = gx::Rect{-1.0, 1.0, -1.0, 1.0}
    };
    const auto bandDirectResult = gx::refineInequalityTile(
        bandDirect,
        gx::TileKey{0, 0, 0},
        bandOptions);
    const auto bandGenericResult = gx::refineInequalityTile(
        bandGeneric,
        gx::TileKey{0, 0, 0},
        bandOptions);

    REQUIRE(bandDirectResult.ok);
    REQUIRE(bandGenericResult.ok);
    CHECK(bandDirectResult.region.pixels == bandGenericResult.region.pixels);
    CHECK(bandDirectResult.region.certainty == bandGenericResult.region.certainty);
    CHECK(bandDirectResult.region.existence == bandGenericResult.region.existence);

    const auto expDirect = gx::FormulaCompiler{}.compile("exp(x)-y>0");
    const auto expGeneric = gx::FormulaCompiler{}.compile("exp(x+0*y)-y>0");
    REQUIRE(expDirect.diagnostics.ok);
    REQUIRE(expGeneric.diagnostics.ok);

    const auto expOptions = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = 32,
        .rootBounds = gx::Rect{-4.0, 4.0, 0.0, 20.0}
    };
    const auto expDirectResult = gx::refineInequalityTile(
        expDirect,
        gx::TileKey{0, 0, 0},
        expOptions);
    const auto expGenericResult = gx::refineInequalityTile(
        expGeneric,
        gx::TileKey{0, 0, 0},
        expOptions);

    REQUIRE(expDirectResult.ok);
    REQUIRE(expGenericResult.ok);
    CHECK(expDirectResult.region.pixels == expGenericResult.region.pixels);
    CHECK(expDirectResult.region.certainty == expGenericResult.region.certainty);
    CHECK(expDirectResult.region.existence == expGenericResult.region.existence);

    const auto rationalDirect = gx::FormulaCompiler{}.compile("y<1/(x-0.001)");
    const auto rationalGeneric = gx::FormulaCompiler{}.compile("y<1/(x+0*y-0.001)");
    REQUIRE(rationalDirect.diagnostics.ok);
    REQUIRE(rationalGeneric.diagnostics.ok);

    const auto rationalOptions = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = 32,
        .rootBounds = gx::Rect{-2.0, 2.0, -5.0, 5.0}
    };
    const auto rationalDirectResult = gx::refineInequalityTile(
        rationalDirect,
        gx::TileKey{0, 0, 0},
        rationalOptions);
    const auto rationalGenericResult = gx::refineInequalityTile(
        rationalGeneric,
        gx::TileKey{0, 0, 0},
        rationalOptions);

    REQUIRE(rationalDirectResult.ok);
    REQUIRE(rationalGenericResult.ok);
    CHECK(rationalDirectResult.region.pixels == rationalGenericResult.region.pixels);
    CHECK(rationalDirectResult.region.certainty == rationalGenericResult.region.certainty);
    CHECK(rationalDirectResult.region.existence == rationalGenericResult.region.existence);

    const auto highFrequencyDirect = gx::FormulaCompiler{}.compile("sin(20*x*y)<sin(sin(10*y))");
    const auto highFrequencyGeneric = gx::FormulaCompiler{}.compile(
        "sin((20+0*x)*x*y)<sin(sin(10*y+0*x))");
    REQUIRE(highFrequencyDirect.diagnostics.ok);
    REQUIRE(highFrequencyGeneric.diagnostics.ok);

    const auto highFrequencyOptions = gx::InequalityTileRefinementOptions{
        .pixelsPerAxis = 32,
        .rootBounds = gx::Rect{-2.0, 2.0, -2.0, 2.0}
    };
    const auto highFrequencyDirectResult = gx::refineInequalityTile(
        highFrequencyDirect,
        gx::TileKey{0, 0, 0},
        highFrequencyOptions);
    const auto highFrequencyGenericResult = gx::refineInequalityTile(
        highFrequencyGeneric,
        gx::TileKey{0, 0, 0},
        highFrequencyOptions);

    REQUIRE(highFrequencyDirectResult.ok);
    REQUIRE(highFrequencyGenericResult.ok);
    CHECK(highFrequencyDirectResult.region.pixels == highFrequencyGenericResult.region.pixels);
    CHECK(highFrequencyDirectResult.region.certainty == highFrequencyGenericResult.region.certainty);
    CHECK(highFrequencyDirectResult.region.existence == highFrequencyGenericResult.region.existence);
}

TEST_CASE("Default ComputeBackend rasterizes through the preferred backend with CPU-equivalent output",
          "[ComputeBackend]")
{
    const std::array formulas{
        "x<=y",
        "sin(x*y)<sin(sin(y))"
    };
    std::array keys{gx::TileKey{0, 0, 0}, gx::TileKey{1, 0, 0}, gx::TileKey{12, 12, 1}};
    std::array xMin{0.0, 1.0, 24.0};
    std::array xMax{1.0, 2.0, 26.0};
    std::array yMin{0.0, 0.0, 24.0};
    std::array yMax{1.0, 1.0, 26.0};
    constexpr auto pixelsPerAxis = uint32_t{16};
    constexpr auto pixelsPerTile = pixelsPerAxis * pixelsPerAxis;
    std::array offsets{uint32_t{0}, pixelsPerTile, pixelsPerTile * 2};

    for (const auto *source : formulas)
    {
        const auto formula = gx::FormulaCompiler{}.compile(source);
        REQUIRE(formula.diagnostics.ok);
        std::array<gx::RegionOutput, 3> preferredOut{};
        std::array<gx::RegionOutput, 3> cpuOut{};

        auto preferred = gx::makeDefaultComputeBackend();
        gx::CpuComputeBackend cpu;

        const auto preferredResult = preferred->rasterizeRegions(
            gx::RasterBatchView{
                &formula,
                keys,
                xMin,
                xMax,
                yMin,
                yMax,
                offsets,
                pixelsPerAxis,
                gx::TexturePreparationMode::Refined
            },
            preferredOut);
        const auto cpuResult = cpu.rasterizeRegions(
            gx::RasterBatchView{
                &formula,
                keys,
                xMin,
                xMax,
                yMin,
                yMax,
                offsets,
                pixelsPerAxis,
                gx::TexturePreparationMode::Refined
            },
            cpuOut);

        REQUIRE(preferredResult.ok);
        REQUIRE(cpuResult.ok);
        REQUIRE(preferredResult.completed == keys.size());
        REQUIRE(cpuResult.completed == keys.size());
        for (size_t index = 0; index < keys.size(); ++index)
        {
            CHECK(preferredOut[index].key == cpuOut[index].key);
            CHECK(preferredOut[index].width == pixelsPerAxis);
            CHECK(preferredOut[index].height == pixelsPerAxis);
            CHECK(preferredOut[index].pixels == cpuOut[index].pixels);
        }
    }
}
