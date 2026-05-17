#ifndef FRAMEPIPELINE_H
#define FRAMEPIPELINE_H

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Compute/ComputeBackend.h"
#include "../Compute/TilePlanner.h"
#include "../Compute/TileRuntime.h"
#include "../Compute/VisualCoverBuilder.h"
#include "../Formula/FormulaCompiler.h"
#include "../Render/FrameCommandBuffer.h"
#include "../Render/RenderResourceManager.h"
#include "../Render/UploadPlanner.h"
#include "../Tile/TileCache.h"
#include "../Util/Contracts.h"
#include "AppState.h"

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
};

class FramePipeline
{
public:
    explicit FramePipeline(std::unique_ptr<ComputeBackend> backend = nullptr);

    [[nodiscard]] FrameSnapshot process(const InputEvent &event);
    [[nodiscard]] const AppState &state() const;
    [[nodiscard]] const TileCache &tiles() const;
    [[nodiscard]] const FramePipelineCounters &counters() const;
    [[nodiscard]] RenderResourceManager &renderResources();

private:
    [[nodiscard]] ViewportRequest makeViewportRequest(const StateDiff &diff) const;
    [[nodiscard]] FrameCommandBuffer buildCommands(std::vector<DisplayTile> &displayTiles,
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
    VisualCoverBuilder visualCoverBuilder{};
    TileRuntime tileRuntime;
    TileCache tileCache{};
    UploadPlanner uploadPlanner{};
    RenderResourceManager resources{};
    std::unordered_map<uint64_t, RegionOutput> regionPayloads;
    std::optional<CommittedVisualFrame> committedVisualFrame{};
    FramePipelineCounters pipelineCounters{};
    FramePipelineDebugStats debugStats{};
    uint64_t frameId{0};
    bool hasRequestedTiles{false};
};
}

#endif // FRAMEPIPELINE_H
