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

TEST_CASE("CpuComputeBackend rasterizer stores subpixel coverage instead of binary samples", "[ComputeBackend]")
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
    CHECK(static_cast<int>(out.front().pixels.front()) == 128);
}

TEST_CASE("CpuComputeBackend rasterizer marks tan pole pixels unresolved", "[ComputeBackend]")
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
    CHECK(out.front().pixels.front() == 127);
}

TEST_CASE("Default ComputeBackend is the concrete CPU backend while OpenCL is ruled out",
          "[ComputeBackend]")
{
    auto backend = gx::makeDefaultComputeBackend();

    REQUIRE(dynamic_cast<gx::CpuComputeBackend *>(backend.get()) != nullptr);
    CHECK_FALSE(backend->capabilities().supportsOpenCl);
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
            gx::RasterBatchView{&formula, keys, xMin, xMax, yMin, yMax, offsets, pixelsPerAxis},
            preferredOut);
        const auto cpuResult = cpu.rasterizeRegions(
            gx::RasterBatchView{&formula, keys, xMin, xMax, yMin, yMax, offsets, pixelsPerAxis},
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
