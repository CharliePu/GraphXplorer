#ifndef FRAMEBUDGETCONTROLLER_H
#define FRAMEBUDGETCONTROLLER_H

#include <chrono>
#include <cstddef>
#include <optional>

#include "../Compute/TileJob.h"
#include "../Tile/TileMath.h"
#include "../Util/Contracts.h"
#include "AppState.h"

namespace gx
{
struct FrameBudgetControllerOptions
{
    std::chrono::microseconds targetPipelineLatency{8000};
    std::chrono::microseconds initialApplyBudget{2000};
    std::chrono::microseconds minApplyBudget{250};
    std::chrono::microseconds maxApplyBudget{4000};
    TilePlanBudget tilePlanBudget{};
    UploadBudget uploadBudget{};
    UploadBudget renderUploadBudget{};
    int maxSeedCells{4};
    size_t maxInFlightJobs{192};
    size_t maxPendingCompletionsBeforeBackpressure{64};
    int refinementDepth{DefaultRefinementDepth};
    int maxRefinementDepth{8};
    bool gpuPreviewAllowed{true};
};

struct FrameWorkBudget
{
    std::chrono::microseconds completedTileApplyBudget{2000};
    TilePlanBudget tilePlan{};
    UploadBudget upload{};
    UploadBudget renderUpload{};
    int maxSeedCells{4};
    int refinementDepth{DefaultRefinementDepth};
    bool submitTileJobs{true};
    bool allowGpuPreview{true};
};

struct FrameBudgetFeedback
{
    std::chrono::microseconds pipelineLatency{0};
    size_t pendingCompletions{0};
    size_t submittedJobs{0};
};

struct FramebufferBudgetSignature
{
    int framebufferWidth{0};
    int framebufferHeight{0};
    double devicePixelRatio{1.0};
};

struct FrameBudgetContext
{
    bool formulaChanged{false};
    std::optional<FramebufferBudgetSignature> framebuffer{};
    size_t pendingCompletions{0};
    size_t inFlightJobs{0};
};

class FrameBudgetPolicy
{
public:
    virtual ~FrameBudgetPolicy() = default;

    [[nodiscard]] virtual FrameWorkBudget beginFrame(const FrameBudgetContext &context) = 0;
    virtual void endFrame(const FrameBudgetFeedback &feedback) = 0;
};

class FrameBudgetController final : public FrameBudgetPolicy
{
public:
    explicit FrameBudgetController(FrameBudgetControllerOptions options = {});

    [[nodiscard]] FrameWorkBudget beginFrame(const FrameBudgetContext &context) override;
    [[nodiscard]] FrameWorkBudget beginFrame(const InputEvent &event,
                                             const StateDiff &diff,
                                             size_t pendingCompletions,
                                             size_t inFlightJobs);
    void endFrame(const FrameBudgetFeedback &feedback) override;

    [[nodiscard]] int dynamicRefinementDepth() const;
    [[nodiscard]] std::chrono::microseconds dynamicApplyBudget() const;

private:
    [[nodiscard]] FrameWorkBudget budgetForFrame(size_t pendingCompletions,
                                                 size_t inFlightJobs) const;
    void resetTopologyPolicy();

    FrameBudgetControllerOptions options{};
    std::chrono::microseconds applyBudget{2000};
    int refinementDepth{DefaultRefinementDepth};
    FramebufferBudgetSignature framebufferSignature{};
    bool hasFramebufferSignature{false};
};
}

#endif // FRAMEBUDGETCONTROLLER_H
