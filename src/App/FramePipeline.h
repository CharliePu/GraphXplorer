#ifndef FRAMEPIPELINE_H
#define FRAMEPIPELINE_H

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Compute/ComputeBackend.h"
#include "../Compute/TilePlanner.h"
#include "../Compute/TileRuntime.h"
#include "../Formula/FormulaCompiler.h"
#include "../Render/FrameCommandBuffer.h"
#include "../Render/RenderResourceManager.h"
#include "../Tile/TileCache.h"
#include "../Util/Contracts.h"
#include "AppState.h"
#include "FrameBudgetController.h"
#include "PresentationPlanner.h"

namespace gx
{
struct FramePipelineCounters
{
    uint64_t formulasCompiled{0};
    uint64_t tileJobsScheduled{0};
    uint64_t tileDeltasApplied{0};
    uint64_t tileDeltasRejected{0};
    uint64_t drawCommandsBuilt{0};

    [[nodiscard]] std::string toDebugString() const;
};

struct FramePipelineDebugStats
{
    size_t displayTiles{0};
    size_t plotTiles{0};
    size_t missingTiles{0};
    size_t mixedTiles{0};
    size_t uniformTiles{0};
    size_t fallbackTiles{0};
    size_t clippedFallbackTiles{0};
    size_t inFlightJobs{0};
    size_t completedJobs{0};
    size_t queuedIntervalTiles{0};
    size_t queuedRegionTiles{0};
    size_t stuckIntervalTiles{0};
    size_t stuckRegionTiles{0};
    size_t submittedJobs{0};
    TilePlanStats tilePlan{};
    int refinementDepth{DefaultRefinementDepth};
    bool allowGpuRaster{true};
};

struct FramePipelineOptions
{
    TileRuntimeOptions tileRuntime{};
    FrameBudgetControllerOptions frameBudget{};
};

class FramePipeline
{
public:
    explicit FramePipeline(std::unique_ptr<ComputeBackend> backend = nullptr,
                           FramePipelineOptions options = {},
                           std::unique_ptr<FrameBudgetPolicy> frameBudgetPolicy = nullptr,
                           std::unique_ptr<BackendBatchPolicy> batchPolicy = nullptr);

    [[nodiscard]] FrameSnapshot process(const InputEvent &event);
    [[nodiscard]] const AppState &state() const;
    [[nodiscard]] const TileCache &tiles() const;
    [[nodiscard]] const FramePipelineCounters &counters() const;
    [[nodiscard]] RenderResourceManager &renderResources();
    [[nodiscard]] const UploadBudget &renderUploadBudget() const;
    void setFrameWakeCallback(std::function<void()> callback);
    [[nodiscard]] size_t pendingCompletionCount() const;
    [[nodiscard]] size_t inFlightCount() const;

private:
    [[nodiscard]] ViewportRequest makeViewportRequest(const StateDiff &diff) const;
    [[nodiscard]] FrameCommandBuffer buildCommands(std::vector<DisplayTile> &displayTiles,
                                                   std::span<const DisplayTile> preloadTiles,
                                                   const ViewportRequest &request,
                                                   const UploadPlan &uploadPlan);
    [[nodiscard]] std::vector<OverlayRect> buildOverlayRects() const;
    [[nodiscard]] std::vector<OverlayTextRun> buildOverlayTextRuns() const;

    AppState appState{};
    AppStateReducer reducer{};
    EffectPlanner effectPlanner{};
    FormulaCompiler formulaCompiler{};
    std::optional<CompiledFormula> compiledFormula{};
    TilePlanner tilePlanner{};
    TileRuntime tileRuntime;
    TileCache tileCache{};
    PresentationPlanner presentationPlanner{};
    RenderResourceManager resources{};
    std::unique_ptr<FrameBudgetPolicy> frameBudgetPolicy;
    FrameWorkBudget latestFrameBudget{};
    std::unordered_map<uint64_t, RegionOutput> regionPayloads;
    std::optional<CommittedVisualFrame> committedVisualFrame{};
    FramePipelineCounters pipelineCounters{};
    FramePipelineDebugStats debugStats{};
    FramePipelineOptions options{};
    uint64_t frameId{0};
    bool hasRequestedTiles{false};
};
}

#endif // FRAMEPIPELINE_H
