#include "FrameBudgetController.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace gx
{
namespace
{
[[nodiscard]] std::chrono::microseconds clampDuration(
    const std::chrono::microseconds value,
    const std::chrono::microseconds lower,
    const std::chrono::microseconds upper)
{
    return std::min(std::max(value, lower), upper);
}

[[nodiscard]] bool nearEqual(const double lhs, const double rhs)
{
    const auto scale = std::max({1.0, std::abs(lhs), std::abs(rhs)});
    return std::abs(lhs - rhs) <= scale * 1e-9;
}

[[nodiscard]] bool sameFramebufferSignature(
    const FramebufferBudgetSignature &lhs,
    const FramebufferBudgetSignature &rhs)
{
    return lhs.framebufferWidth == rhs.framebufferWidth
        && lhs.framebufferHeight == rhs.framebufferHeight
        && nearEqual(lhs.devicePixelRatio, rhs.devicePixelRatio);
}

[[nodiscard]] FramebufferBudgetSignature signatureForViewport(
    const ViewportChangedEvent &viewport)
{
    return {
        .framebufferWidth = viewport.framebufferWidth,
        .framebufferHeight = viewport.framebufferHeight,
        .devicePixelRatio = viewport.devicePixelRatio
    };
}
}

FrameBudgetController::FrameBudgetController(FrameBudgetControllerOptions nextOptions)
    : options{nextOptions},
      applyBudget{nextOptions.initialApplyBudget}
{
    options.targetPipelineLatency = std::max(options.targetPipelineLatency, std::chrono::microseconds{1});
    options.minApplyBudget = std::max(options.minApplyBudget, std::chrono::microseconds{1});
    options.maxApplyBudget = std::max(options.maxApplyBudget, options.minApplyBudget);
    options.initialApplyBudget =
        clampDuration(options.initialApplyBudget, options.minApplyBudget, options.maxApplyBudget);
    options.maxSeedCells = std::max(1, options.maxSeedCells);
    options.maxInFlightJobs = std::max<size_t>(1, options.maxInFlightJobs);
    options.maxPendingCompletionsBeforeBackpressure =
        std::max<size_t>(1, options.maxPendingCompletionsBeforeBackpressure);
    options.maxRefinementDepth = std::max(0, options.maxRefinementDepth);
    options.refinementDepth = std::clamp(options.refinementDepth, 0, options.maxRefinementDepth);
    applyBudget = options.initialApplyBudget;
    refinementDepth = options.refinementDepth;
}

FrameWorkBudget FrameBudgetController::beginFrame(const InputEvent &event,
                                                  const StateDiff &diff,
                                                  const size_t pendingCompletions,
                                                  const size_t inFlightJobs)
{
    auto context = FrameBudgetContext{
        .formulaChanged = diff.formulaChanged,
        .pendingCompletions = pendingCompletions,
        .inFlightJobs = inFlightJobs
    };
    if (const auto *viewport = std::get_if<ViewportChangedEvent>(&event))
    {
        context.framebuffer = signatureForViewport(*viewport);
    }

    return beginFrame(context);
}

FrameWorkBudget FrameBudgetController::beginFrame(const FrameBudgetContext &context)
{
    auto framebufferChanged = false;
    if (context.framebuffer)
    {
        framebufferChanged = !hasFramebufferSignature
            || !sameFramebufferSignature(framebufferSignature, *context.framebuffer);
        framebufferSignature = *context.framebuffer;
        hasFramebufferSignature = true;
    }

    if (context.formulaChanged || framebufferChanged)
    {
        resetTopologyPolicy();
    }

    return budgetForFrame(context.pendingCompletions, context.inFlightJobs);
}

void FrameBudgetController::endFrame(const FrameBudgetFeedback &feedback)
{
    const auto highLatency = feedback.pipelineLatency > options.targetPipelineLatency;
    const auto lowLatency = feedback.pipelineLatency < options.targetPipelineLatency * 3 / 5;
    const auto hasBacklog = feedback.pendingCompletions > 0 || feedback.submittedJobs > 0;

    if (highLatency)
    {
        applyBudget = clampDuration(applyBudget / 2, options.minApplyBudget, options.maxApplyBudget);
        return;
    }

    if (lowLatency && hasBacklog)
    {
        applyBudget = clampDuration(applyBudget + std::chrono::microseconds{250},
                                    options.minApplyBudget,
                                    options.maxApplyBudget);
    }
    else if (!hasBacklog)
    {
        applyBudget = clampDuration(options.initialApplyBudget, options.minApplyBudget, options.maxApplyBudget);
    }
}

int FrameBudgetController::dynamicRefinementDepth() const
{
    return refinementDepth;
}

std::chrono::microseconds FrameBudgetController::dynamicApplyBudget() const
{
    return applyBudget;
}

FrameWorkBudget FrameBudgetController::budgetForFrame(const size_t pendingCompletions,
                                                      const size_t inFlightJobs) const
{
    auto budget = FrameWorkBudget{
        .completedTileApplyBudget = applyBudget,
        .tilePlan = options.tilePlanBudget,
        .upload = options.uploadBudget,
        .renderUpload = options.renderUploadBudget,
        .maxSeedCells = options.maxSeedCells,
        .refinementDepth = refinementDepth,
        .submitTileJobs = true,
        .allowGpuRaster = options.gpuRasterAllowed
    };

    budget.completedTileApplyBudget = clampDuration(
        budget.completedTileApplyBudget,
        options.minApplyBudget,
        options.maxApplyBudget);
    budget.refinementDepth = std::clamp(budget.refinementDepth, 0, options.maxRefinementDepth);

    const auto completionBacklogFull = pendingCompletions >= options.maxPendingCompletionsBeforeBackpressure;
    if (completionBacklogFull || inFlightJobs >= options.maxInFlightJobs)
    {
        budget.submitTileJobs = false;
        budget.tilePlan.maxIntervalJobsPerFrame = 0;
        budget.tilePlan.maxRasterJobsPerFrame = 0;
        return budget;
    }

    auto remainingJobHeadroom = options.maxInFlightJobs - inFlightJobs;
    const auto rasterBudget = std::min<size_t>(
        static_cast<size_t>(std::max(0, budget.tilePlan.maxRasterJobsPerFrame)),
        remainingJobHeadroom);
    budget.tilePlan.maxRasterJobsPerFrame = static_cast<int>(rasterBudget);
    remainingJobHeadroom -= rasterBudget;
    const auto intervalBudget = std::min<size_t>(
        static_cast<size_t>(std::max(0, budget.tilePlan.maxIntervalJobsPerFrame)),
        remainingJobHeadroom);
    budget.tilePlan.maxIntervalJobsPerFrame = static_cast<int>(intervalBudget);
    budget.submitTileJobs = budget.tilePlan.maxIntervalJobsPerFrame > 0
        || budget.tilePlan.maxRasterJobsPerFrame > 0;

    return budget;
}

void FrameBudgetController::resetTopologyPolicy()
{
    refinementDepth = options.refinementDepth;
}
}
