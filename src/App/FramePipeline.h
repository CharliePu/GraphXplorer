#ifndef FRAMEPIPELINE_H
#define FRAMEPIPELINE_H

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

class FramePipeline
{
public:
    explicit FramePipeline(std::unique_ptr<ComputeBackend> backend = std::make_unique<CpuComputeBackend>());

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
    uint64_t frameId{0};
    bool hasRequestedTiles{false};
};
}

#endif // FRAMEPIPELINE_H
