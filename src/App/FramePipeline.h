#ifndef FRAMEPIPELINE_H
#define FRAMEPIPELINE_H

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "../Compute/ComputeBackend.h"
#include "../Compute/TileScheduler.h"
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
    [[nodiscard]] TileTransaction executeVisibleJobs(const ViewportRequest &request,
                                                     const std::vector<TileJob> &jobs);
    [[nodiscard]] FrameCommandBuffer buildCommands(const std::vector<TileKey> &visibleCover,
                                                   const ViewportRequest &request);

    AppState appState{};
    AppStateReducer reducer{};
    EffectPlanner effectPlanner{};
    FormulaCompiler formulaCompiler{};
    std::optional<CompiledFormula> compiledFormula{};
    TileScheduler scheduler{};
    std::unique_ptr<ComputeBackend> backend;
    TileCache tileCache{};
    UploadPlanner uploadPlanner{};
    RenderResourceManager resources{};
    std::unordered_map<uint64_t, RegionOutput> regionPayloads;
    FramePipelineCounters pipelineCounters{};
    uint64_t frameId{0};
    uint64_t nextRegionPayloadId{1};
    bool hasRequestedTiles{false};
};
}

#endif // FRAMEPIPELINE_H
