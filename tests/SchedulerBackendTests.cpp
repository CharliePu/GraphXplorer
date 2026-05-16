#include "catch.hpp"

#include <array>

#include "../src/Compute/ComputeBackend.h"
#include "../src/Compute/TileScheduler.h"

TEST_CASE("TileScheduler emits dependency-ordered visible tile jobs", "[Scheduler]")
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

    const auto jobs = gx::TileScheduler{}.buildJobs(request);
    REQUIRE_FALSE(jobs.empty());
    CHECK(jobs.front().kind == gx::JobKind::ClassifyInterval);
    CHECK(jobs.front().workClass == gx::WorkClass::VisibleNow);
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
