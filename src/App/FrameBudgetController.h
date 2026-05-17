#ifndef FRAMEBUDGETCONTROLLER_H
#define FRAMEBUDGETCONTROLLER_H

#include <chrono>
#include <cstddef>

#include "../Compute/TileJob.h"
#include "../Tile/TileMath.h"
#include "../Util/Contracts.h"
#include "AppState.h"

namespace gx
{
struct FrameBudgetControllerOptions
{
    std::chrono::microseconds targetPipelineLatency{8000};
    std::chrono::microseconds interactionHold{150000};
    std::chrono::microseconds steadyApplyBudget{2000};
    std::chrono::microseconds interactiveApplyBudget{500};
    std::chrono::microseconds minApplyBudget{250};
    std::chrono::microseconds maxApplyBudget{4000};
    TilePlanBudget steadyTilePlanBudget{};
    TilePlanBudget interactiveTilePlanBudget{48, 8};
    UploadBudget steadyUploadBudget{};
    UploadBudget interactiveUploadBudget{1024 * 1024, 256 * 1024, 8, 512};
    UploadBudget steadyRenderUploadBudget{};
    UploadBudget interactiveRenderUploadBudget{1024 * 1024, 256 * 1024, 8, 512};
    int steadyMaxSeedCells{4};
    int interactiveMaxSeedCells{4};
    size_t steadyMaxInFlightJobs{192};
    size_t interactiveMaxInFlightJobs{48};
    size_t maxPendingCompletionsBeforeBackpressure{64};
    int refinementDepth{DefaultRefinementDepth};
    int maxRefinementDepth{8};
};

struct FrameWorkBudget
{
    std::chrono::microseconds completedTileApplyBudget{2000};
    TilePlanBudget tilePlan{};
    UploadBudget upload{};
    UploadBudget renderUpload{};
    int maxSeedCells{4};
    int refinementDepth{DefaultRefinementDepth};
    bool interactive{false};
    bool submitTileJobs{true};
};

struct FrameBudgetFeedback
{
    std::chrono::microseconds pipelineLatency{0};
    size_t pendingCompletions{0};
    size_t submittedJobs{0};
    size_t displayTiles{0};
    size_t missingTiles{0};
};

struct FramebufferBudgetSignature
{
    int framebufferWidth{0};
    int framebufferHeight{0};
    double devicePixelRatio{1.0};
};

class FrameBudgetController
{
public:
    explicit FrameBudgetController(FrameBudgetControllerOptions options = {});

    [[nodiscard]] FrameWorkBudget beginFrame(const InputEvent &event,
                                             const StateDiff &diff,
                                             size_t pendingCompletions,
                                             size_t inFlightJobs);
    void endFrame(const FrameBudgetFeedback &feedback);

    [[nodiscard]] int dynamicRefinementDepth() const;
    [[nodiscard]] std::chrono::microseconds dynamicApplyBudget() const;

private:
    [[nodiscard]] bool recentlyInteractive(std::chrono::steady_clock::time_point now) const;
    [[nodiscard]] FrameWorkBudget budgetForMode(bool interactive,
                                                size_t pendingCompletions,
                                                size_t inFlightJobs) const;
    void resetTopologyPolicy();

    FrameBudgetControllerOptions options{};
    std::chrono::steady_clock::time_point lastInteraction{};
    std::chrono::microseconds applyBudget{2000};
    int refinementDepth{DefaultRefinementDepth};
    bool lastFrameInteractive{false};
    bool holdInteractiveBudgetsUntilPresentable{false};
    FramebufferBudgetSignature framebufferSignature{};
    bool hasFramebufferSignature{false};
};
}

#endif // FRAMEBUDGETCONTROLLER_H
