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

TEST_CASE("FrameBudgetController uses one configured budget path",
          "[FrameBudgetController][Responsiveness]")
{
    gx::FrameBudgetController controller{gx::FrameBudgetControllerOptions{
        .initialApplyBudget = 1500us,
        .minApplyBudget = 250us,
        .maxApplyBudget = 4000us,
        .tilePlanBudget = {256, 64},
        .renderUploadBudget = {
            .maxTextureBytesPerFrame = 4 * 1024 * 1024,
            .maxTextureSlicesPerFrame = 32
        },
        .maxSeedCells = 6,
        .maxInFlightJobs = 512,
        .refinementDepth = 3,
        .maxRefinementDepth = 8
    }};

    const auto budget = controller.beginFrame(viewportEvent(), viewportDiff(), 0, 0);

    CHECK(budget.completedTileApplyBudget == 1500us);
    CHECK(budget.tilePlan.maxIntervalJobsPerFrame == 256);
    CHECK(budget.tilePlan.maxRasterJobsPerFrame == 64);
    CHECK(budget.renderUpload.maxTextureSlicesPerFrame == 32);
    CHECK(budget.renderUpload.maxTextureBytesPerFrame == 4 * 1024 * 1024);
    CHECK(budget.maxSeedCells == 6);
    CHECK(budget.refinementDepth == 3);
    CHECK(budget.allowGpuRaster);
}

TEST_CASE("FrameBudgetController does not switch budgets between viewport and render ticks",
          "[FrameBudgetController][Responsiveness]")
{
    gx::FrameBudgetController controller{gx::FrameBudgetControllerOptions{
        .initialApplyBudget = 1200us,
        .tilePlanBudget = {128, 32},
        .maxInFlightJobs = 256,
        .refinementDepth = 3,
        .maxRefinementDepth = 8
    }};

    auto budget = controller.beginFrame(viewportEvent(), viewportDiff(), 0, 0);
    CHECK(budget.completedTileApplyBudget == 1200us);
    CHECK(budget.tilePlan.maxIntervalJobsPerFrame == 128);
    CHECK(budget.tilePlan.maxRasterJobsPerFrame == 32);

    controller.endFrame(gx::FrameBudgetFeedback{
        .pipelineLatency = 2000us,
        .pendingCompletions = 0,
        .submittedJobs = 0
    });

    budget = controller.beginFrame(gx::RenderTickEvent{}, gx::StateDiff{}, 0, 0);
    CHECK(budget.completedTileApplyBudget == 1200us);
    CHECK(budget.tilePlan.maxIntervalJobsPerFrame == 128);
    CHECK(budget.tilePlan.maxRasterJobsPerFrame == 32);
}

TEST_CASE("FrameBudgetController exposes render upload as a responsiveness knob",
          "[FrameBudgetController][Responsiveness]")
{
    gx::FrameBudgetController controller{gx::FrameBudgetControllerOptions{
        .renderUploadBudget = {
            .maxTextureBytesPerFrame = 8 * 1024 * 1024,
            .maxTextureSlicesPerFrame = 64
        }
    }};

    const auto budget = controller.beginFrame(viewportEvent(), viewportDiff(), 0, 0);

    CHECK(budget.renderUpload.maxTextureBytesPerFrame == 8 * 1024 * 1024);
    CHECK(budget.renderUpload.maxTextureSlicesPerFrame == 64);
}

TEST_CASE("FrameBudgetController slow frames reduce apply budget only",
          "[FrameBudgetController][Responsiveness]")
{
    gx::FrameBudgetController controller{gx::FrameBudgetControllerOptions{
        .targetPipelineLatency = 8000us,
        .initialApplyBudget = 2000us,
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
        .tilePlanBudget = {256, 64},
        .maxInFlightJobs = 16,
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
        .tilePlanBudget = {32, 8},
        .maxInFlightJobs = 36
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
        .refinementDepth = 3,
        .maxRefinementDepth = 8
    }};

    auto budget = controller.beginFrame(viewportEvent(), viewportDiff(), 0, 0);
    CHECK(budget.refinementDepth == 3);

    controller.endFrame(gx::FrameBudgetFeedback{
        .pipelineLatency = 2000us,
        .pendingCompletions = 0,
        .submittedJobs = 0
    });

    budget = controller.beginFrame(
        viewportEvent(Interval{7.0, 9.0}, Interval{-4.0, -2.0}),
        viewportDiff(),
        0,
        0);
    CHECK(budget.refinementDepth == 3);

    controller.endFrame(gx::FrameBudgetFeedback{
        .pipelineLatency = 2000us,
        .pendingCompletions = 0,
        .submittedJobs = 0
    });

    budget = controller.beginFrame(
        viewportEvent(Interval{-4.0, 4.0}, Interval{-4.0, 4.0}),
        viewportDiff(),
        0,
        0);
    CHECK(budget.refinementDepth == 3);
}

TEST_CASE("FrameBudgetController keeps GPU raster admission as a direct budget constraint",
          "[FrameBudgetController][Responsiveness]")
{
    gx::FrameBudgetController controller{gx::FrameBudgetControllerOptions{
        .gpuRasterAllowed = false
    }};

    const auto budget = controller.beginFrame(viewportEvent(), viewportDiff(), 0, 0);

    CHECK_FALSE(budget.allowGpuRaster);
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
