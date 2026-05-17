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
      applyBudget{nextOptions.steadyApplyBudget}
{
    options.targetPipelineLatency = std::max(options.targetPipelineLatency, std::chrono::microseconds{1});
    options.interactionHold = std::max(options.interactionHold, std::chrono::microseconds{0});
    options.minApplyBudget = std::max(options.minApplyBudget, std::chrono::microseconds{1});
    options.maxApplyBudget = std::max(options.maxApplyBudget, options.minApplyBudget);
    options.steadyApplyBudget = clampDuration(options.steadyApplyBudget, options.minApplyBudget, options.maxApplyBudget);
    options.interactiveApplyBudget = clampDuration(
        options.interactiveApplyBudget,
        options.minApplyBudget,
        options.maxApplyBudget);
    options.steadyMaxSeedCells = std::max(1, options.steadyMaxSeedCells);
    options.interactiveMaxSeedCells = std::max(1, options.interactiveMaxSeedCells);
    options.steadyMaxInFlightJobs = std::max<size_t>(1, options.steadyMaxInFlightJobs);
    options.interactiveMaxInFlightJobs = std::max<size_t>(1, options.interactiveMaxInFlightJobs);
    options.maxPendingCompletionsBeforeBackpressure =
        std::max<size_t>(1, options.maxPendingCompletionsBeforeBackpressure);
    options.maxRefinementDepth = std::max(0, options.maxRefinementDepth);
    options.refinementDepth = std::clamp(options.refinementDepth, 0, options.maxRefinementDepth);
    applyBudget = options.steadyApplyBudget;
    refinementDepth = options.refinementDepth;
}

FrameWorkBudget FrameBudgetController::beginFrame(const InputEvent &event,
                                                  const StateDiff &diff,
                                                  const size_t pendingCompletions,
                                                  const size_t inFlightJobs)
{
    const auto now = std::chrono::steady_clock::now();
    if (diff.viewportChanged || std::holds_alternative<ViewportChangedEvent>(event))
    {
        lastInteraction = now;
    }

    auto framebufferChanged = false;
    if (const auto *viewport = std::get_if<ViewportChangedEvent>(&event))
    {
        const auto signature = signatureForViewport(*viewport);
        framebufferChanged = !hasFramebufferSignature
            || !sameFramebufferSignature(framebufferSignature, signature);
        framebufferSignature = signature;
        hasFramebufferSignature = true;
    }

    if (diff.formulaChanged || framebufferChanged)
    {
        resetTopologyPolicy();
        holdInteractiveBudgetsUntilPresentable = false;
    }

    const auto interactive = recentlyInteractive(now) || holdInteractiveBudgetsUntilPresentable;
    lastFrameInteractive = interactive;
    return budgetForMode(interactive, pendingCompletions, inFlightJobs);
}

void FrameBudgetController::endFrame(const FrameBudgetFeedback &feedback)
{
    const auto highLatency = feedback.pipelineLatency > options.targetPipelineLatency;
    const auto lowLatency = feedback.pipelineLatency < options.targetPipelineLatency * 3 / 5;
    const auto hasBacklog = feedback.pendingCompletions > 0 || feedback.submittedJobs > 0;

    if (lastFrameInteractive)
    {
        holdInteractiveBudgetsUntilPresentable = feedback.missingTiles > 0;
        if (highLatency)
        {
            applyBudget = clampDuration(applyBudget / 2, options.minApplyBudget, options.maxApplyBudget);
        }
        return;
    }

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
        applyBudget = clampDuration(options.steadyApplyBudget, options.minApplyBudget, options.maxApplyBudget);
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

bool FrameBudgetController::recentlyInteractive(const std::chrono::steady_clock::time_point now) const
{
    return lastInteraction.time_since_epoch().count() != 0
        && now - lastInteraction <= options.interactionHold;
}

FrameWorkBudget FrameBudgetController::budgetForMode(const bool interactive,
                                                     const size_t pendingCompletions,
                                                     const size_t inFlightJobs) const
{
    auto budget = FrameWorkBudget{
        .completedTileApplyBudget = interactive
            ? std::min(applyBudget, options.interactiveApplyBudget)
            : applyBudget,
        .tilePlan = interactive ? options.interactiveTilePlanBudget : options.steadyTilePlanBudget,
        .upload = interactive ? options.interactiveUploadBudget : options.steadyUploadBudget,
        .renderUpload = interactive ? options.interactiveRenderUploadBudget : options.steadyRenderUploadBudget,
        .maxSeedCells = interactive ? options.interactiveMaxSeedCells : options.steadyMaxSeedCells,
        .refinementDepth = refinementDepth,
        .interactive = interactive,
        .submitTileJobs = true
    };

    budget.completedTileApplyBudget = clampDuration(
        budget.completedTileApplyBudget,
        options.minApplyBudget,
        options.maxApplyBudget);
    budget.refinementDepth = std::clamp(budget.refinementDepth, 0, options.maxRefinementDepth);

    const auto maxInFlightJobs = interactive
        ? options.interactiveMaxInFlightJobs
        : options.steadyMaxInFlightJobs;
    const auto completionBacklogFull = pendingCompletions >= options.maxPendingCompletionsBeforeBackpressure;
    if (completionBacklogFull || inFlightJobs >= maxInFlightJobs)
    {
        budget.submitTileJobs = false;
        budget.tilePlan.maxIntervalJobsPerFrame = 0;
        budget.tilePlan.maxRasterJobsPerFrame = 0;
        return budget;
    }

    auto remainingJobHeadroom = maxInFlightJobs - inFlightJobs;
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

    if (interactive && pendingCompletions > 0)
    {
        budget.tilePlan.maxRasterJobsPerFrame = std::min(budget.tilePlan.maxRasterJobsPerFrame, 4);
        budget.submitTileJobs = budget.tilePlan.maxIntervalJobsPerFrame > 0
            || budget.tilePlan.maxRasterJobsPerFrame > 0;
    }

    return budget;
}

void FrameBudgetController::resetTopologyPolicy()
{
    refinementDepth = options.refinementDepth;
}
}
