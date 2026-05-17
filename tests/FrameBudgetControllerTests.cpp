#include "catch.hpp"

#include <chrono>

#include "../src/App/FrameBudgetController.h"

using namespace std::chrono_literals;

namespace
{
gx::StateDiff viewportDiff()
{
    return {
        .viewportChanged = true,
        .renderInvalidated = true,
        .requestId = 2,
        .generation = 1
    };
}

gx::ViewportChangedEvent viewportEvent(const Interval &xRange = {-1.0, 1.0},
                                       const Interval &yRange = {-1.0, 1.0},
                                       const int width = 512,
                                       const int height = 512)
{
    return {
        .xRange = xRange,
        .yRange = yRange,
        .framebufferWidth = width,
        .framebufferHeight = height
    };
}
}

TEST_CASE("FrameBudgetController uses interactive budgets without changing refinement depth",
          "[FrameBudgetController][Responsiveness]")
{
    gx::FrameBudgetController controller{gx::FrameBudgetControllerOptions{
        .steadyApplyBudget = 2000us,
        .interactiveApplyBudget = 500us,
        .steadyTilePlanBudget = {256, 64},
        .interactiveTilePlanBudget = {32, 4},
        .refinementDepth = 3,
        .maxRefinementDepth = 8
    }};

    const auto budget = controller.beginFrame(viewportEvent(), viewportDiff(), 0, 0);

    CHECK(budget.interactive);
    CHECK(budget.completedTileApplyBudget == 500us);
    CHECK(budget.tilePlan.maxIntervalJobsPerFrame == 32);
    CHECK(budget.tilePlan.maxRasterJobsPerFrame == 4);
    CHECK(budget.renderUpload.maxTextureSlicesPerFrame == 8);
    CHECK(budget.renderUpload.maxTextureBytesPerFrame == 1024 * 1024);
    CHECK(budget.refinementDepth == 3);
}

TEST_CASE("FrameBudgetController exposes render upload as a responsiveness knob",
          "[FrameBudgetController][Responsiveness]")
{
    gx::FrameBudgetController controller{gx::FrameBudgetControllerOptions{
        .interactionHold = 0us,
        .steadyRenderUploadBudget = {
            .maxTextureBytesPerFrame = 8 * 1024 * 1024,
            .maxTextureSlicesPerFrame = 64
        },
        .interactiveRenderUploadBudget = {
            .maxTextureBytesPerFrame = 128 * 1024,
            .maxTextureSlicesPerFrame = 2
        }
    }};

    auto budget = controller.beginFrame(viewportEvent(), viewportDiff(), 0, 0);
    CHECK(budget.interactive);
    CHECK(budget.renderUpload.maxTextureBytesPerFrame == 128 * 1024);
    CHECK(budget.renderUpload.maxTextureSlicesPerFrame == 2);

    controller.endFrame(gx::FrameBudgetFeedback{
        .pipelineLatency = 2000us,
        .missingTiles = 0
    });

    budget = controller.beginFrame(gx::RenderTickEvent{}, gx::StateDiff{}, 0, 0);
    CHECK_FALSE(budget.interactive);
    CHECK(budget.renderUpload.maxTextureBytesPerFrame == 8 * 1024 * 1024);
    CHECK(budget.renderUpload.maxTextureSlicesPerFrame == 64);
}

TEST_CASE("FrameBudgetController slow frames reduce apply budget only",
          "[FrameBudgetController][Responsiveness]")
{
    gx::FrameBudgetController controller{gx::FrameBudgetControllerOptions{
        .targetPipelineLatency = 8000us,
        .steadyApplyBudget = 2000us,
        .minApplyBudget = 250us,
        .maxApplyBudget = 4000us,
        .refinementDepth = 3,
        .maxRefinementDepth = 8
    }};

    controller.endFrame(gx::FrameBudgetFeedback{
        .pipelineLatency = 12000us,
        .pendingCompletions = 10,
        .submittedJobs = 10
    });
    controller.endFrame(gx::FrameBudgetFeedback{
        .pipelineLatency = 12000us,
        .pendingCompletions = 10,
        .submittedJobs = 10
    });

    CHECK(controller.dynamicRefinementDepth() == 3);
    CHECK(controller.dynamicApplyBudget() == 500us);
}

TEST_CASE("FrameBudgetController applies backpressure when compute is already saturated",
          "[FrameBudgetController][Responsiveness]")
{
    gx::FrameBudgetController controller{gx::FrameBudgetControllerOptions{
        .steadyTilePlanBudget = {256, 64},
        .interactiveTilePlanBudget = {32, 8},
        .steadyMaxInFlightJobs = 128,
        .interactiveMaxInFlightJobs = 16,
        .maxPendingCompletionsBeforeBackpressure = 8
    }};

    auto budget = controller.beginFrame(viewportEvent(), viewportDiff(), 0, 20);
    CHECK_FALSE(budget.submitTileJobs);
    CHECK(budget.tilePlan.maxIntervalJobsPerFrame == 0);
    CHECK(budget.tilePlan.maxRasterJobsPerFrame == 0);

    budget = controller.beginFrame(viewportEvent(), viewportDiff(), 8, 0);
    CHECK_FALSE(budget.submitTileJobs);
    CHECK(budget.tilePlan.maxIntervalJobsPerFrame == 0);
    CHECK(budget.tilePlan.maxRasterJobsPerFrame == 0);
}

TEST_CASE("FrameBudgetController limits per-frame admissions to remaining in-flight headroom",
          "[FrameBudgetController][Responsiveness]")
{
    gx::FrameBudgetController controller{gx::FrameBudgetControllerOptions{
        .steadyTilePlanBudget = {256, 64},
        .interactiveTilePlanBudget = {32, 8},
        .steadyMaxInFlightJobs = 128,
        .interactiveMaxInFlightJobs = 36
    }};

    const auto budget = controller.beginFrame(viewportEvent(), viewportDiff(), 0, 20);

    CHECK(budget.submitTileJobs);
    CHECK(budget.tilePlan.maxIntervalJobsPerFrame == 8);
    CHECK(budget.tilePlan.maxRasterJobsPerFrame == 8);
}

TEST_CASE("FrameBudgetController keeps refinement depth across pan and zoom",
          "[FrameBudgetController][Responsiveness]")
{
    gx::FrameBudgetController controller{gx::FrameBudgetControllerOptions{
        .interactionHold = 0us,
        .refinementDepth = 3,
        .maxRefinementDepth = 8
    }};

    auto budget = controller.beginFrame(viewportEvent(), viewportDiff(), 0, 0);
    REQUIRE(budget.interactive);
    CHECK(budget.refinementDepth == 3);

    controller.endFrame(gx::FrameBudgetFeedback{
        .pipelineLatency = 2000us,
        .pendingCompletions = 0,
        .submittedJobs = 0,
        .missingTiles = 0
    });

    budget = controller.beginFrame(
        viewportEvent(Interval{7.0, 9.0}, Interval{-4.0, -2.0}),
        viewportDiff(),
        0,
        0);
    CHECK(budget.interactive);
    CHECK(budget.refinementDepth == 3);

    controller.endFrame(gx::FrameBudgetFeedback{
        .pipelineLatency = 2000us,
        .pendingCompletions = 0,
        .submittedJobs = 0,
        .missingTiles = 0
    });

    budget = controller.beginFrame(
        viewportEvent(Interval{-4.0, 4.0}, Interval{-4.0, 4.0}),
        viewportDiff(),
        0,
        0);
    CHECK(budget.interactive);
    CHECK(budget.refinementDepth == 3);
}

TEST_CASE("FrameBudgetController keeps interactive budgets until a moving cover is presentable",
          "[FrameBudgetController][Responsiveness]")
{
    gx::FrameBudgetController controller{gx::FrameBudgetControllerOptions{
        .interactionHold = 0us,
        .refinementDepth = 3,
        .maxRefinementDepth = 8
    }};

    auto budget = controller.beginFrame(viewportEvent(), viewportDiff(), 0, 0);
    REQUIRE(budget.interactive);
    CHECK(budget.refinementDepth == 3);

    controller.endFrame(gx::FrameBudgetFeedback{
        .pipelineLatency = 2000us,
        .missingTiles = 4
    });

    budget = controller.beginFrame(gx::RenderTickEvent{}, gx::StateDiff{}, 0, 0);
    CHECK(budget.interactive);
    CHECK(budget.refinementDepth == 3);

    controller.endFrame(gx::FrameBudgetFeedback{
        .pipelineLatency = 2000us,
        .missingTiles = 0
    });

    budget = controller.beginFrame(gx::RenderTickEvent{}, gx::StateDiff{}, 0, 0);
    CHECK_FALSE(budget.interactive);
    CHECK(budget.refinementDepth == 3);
}

TEST_CASE("FrameBudgetController clamps configured refinement depth",
          "[FrameBudgetController][Responsiveness]")
{
    gx::FrameBudgetController controller{gx::FrameBudgetControllerOptions{
        .refinementDepth = 20,
        .maxRefinementDepth = 4
    }};

    const auto budget = controller.beginFrame(viewportEvent(), viewportDiff(), 0, 0);

    CHECK(controller.dynamicRefinementDepth() == 4);
    CHECK(budget.refinementDepth == 4);
}
