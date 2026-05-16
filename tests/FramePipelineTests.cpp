#include "catch.hpp"

#include "../src/App/FramePipeline.h"

TEST_CASE("FramePipeline drives contract-first flow from input event to frame snapshot", "[FramePipeline]")
{
    gx::FramePipeline pipeline;

    const auto snapshot = pipeline.process(gx::ViewportChangedEvent{
        .xRange = Interval{-2.0, 2.0},
        .yRange = Interval{-2.0, 2.0},
        .framebufferWidth = 512,
        .framebufferHeight = 512
    });

    REQUIRE(snapshot.viewportRequest.has_value());
    REQUIRE(snapshot.formula.has_value());
    CHECK(snapshot.viewportRequest->valid());
    CHECK_FALSE(snapshot.appliedTransactions.empty());
    CHECK(snapshot.appliedTransactions.front().valid());
    CHECK_FALSE(snapshot.visibleCover.empty());
    CHECK_FALSE(snapshot.drawCommands.empty());
    CHECK(snapshot.drawCommands.front().layer == gx::RenderLayer::Plot);
    CHECK(snapshot.counters.find("tileDeltasApplied=") != std::string::npos);
}

TEST_CASE("FramePipeline compiles only on formula changes", "[FramePipeline]")
{
    gx::FramePipeline pipeline;
    const auto initialCompiles = pipeline.counters().formulasCompiled;

    const auto viewportSnapshot = pipeline.process(gx::ViewportChangedEvent{
        .xRange = Interval{-3.0, 3.0},
        .yRange = Interval{-3.0, 3.0},
        .framebufferWidth = 512,
        .framebufferHeight = 512
    });
    (void)viewportSnapshot;
    CHECK(pipeline.counters().formulasCompiled == initialCompiles);

    const auto formulaSnapshot = pipeline.process(gx::FormulaInputEvent{.expression = "x+y>0"});
    (void)formulaSnapshot;
    CHECK(pipeline.counters().formulasCompiled == initialCompiles + 1);
}
