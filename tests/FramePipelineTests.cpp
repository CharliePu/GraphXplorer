#include "catch.hpp"

#include <algorithm>

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

TEST_CASE("FramePipeline exposes formula input overlay as command data", "[FramePipeline]")
{
    gx::FramePipeline pipeline;

    auto snapshot = pipeline.process(gx::BeginFormulaInputEvent{});
    CHECK(pipeline.state().formulaInput.active);
    CHECK_FALSE(snapshot.drawCommands.empty());
    CHECK(std::ranges::any_of(snapshot.drawCommands, [](const gx::DrawCommand &command) {
        return command.layer == gx::RenderLayer::Text;
    }));

    const auto appendSnapshot = pipeline.process(gx::AppendFormulaInputEvent{" + x"});
    (void)appendSnapshot;
    snapshot = pipeline.process(gx::SubmitFormulaInputEvent{});

    CHECK_FALSE(pipeline.state().formulaInput.active);
    CHECK(pipeline.state().formulaExpression.find("+ x") != std::string::npos);
    CHECK(snapshot.counters.find("formulasCompiled=") != std::string::npos);
}

TEST_CASE("FramePipeline debug mode adds chunk-frame instances through command renderer", "[FramePipeline]")
{
    gx::FramePipeline pipeline;

    auto snapshot = pipeline.process(gx::ViewportChangedEvent{
        .xRange = Interval{-2.0, 2.0},
        .yRange = Interval{-2.0, 2.0},
        .framebufferWidth = 512,
        .framebufferHeight = 512
    });
    REQUIRE_FALSE(snapshot.visibleCover.empty());
    const auto normalInstanceCount = pipeline.renderResources().plotInstanceCount();
    REQUIRE(normalInstanceCount == snapshot.visibleCover.size());

    snapshot = pipeline.process(gx::DebugToggleEvent{true});
    CHECK(pipeline.state().debug);
    const auto debugInstanceCount = pipeline.renderResources().debugPlotInstanceCount();
    CHECK(pipeline.renderResources().plotInstanceCount() == normalInstanceCount);
    CHECK(debugInstanceCount == normalInstanceCount);
    CHECK(std::ranges::any_of(snapshot.drawCommands, [debugInstanceCount](const gx::DrawCommand &command) {
        return command.layer == gx::RenderLayer::Contour
            && command.instanceRange.count == static_cast<uint32_t>(debugInstanceCount);
    }));
}
