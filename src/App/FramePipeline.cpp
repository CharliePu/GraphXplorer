#include "FramePipeline.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "../Tile/TileMath.h"

namespace gx
{
std::string FramePipelineCounters::toDebugString() const
{
    std::ostringstream out;
    out << "formulasCompiled=" << formulasCompiled
        << ",tileJobsScheduled=" << tileJobsScheduled
        << ",tileDeltasApplied=" << tileDeltasApplied
        << ",tileDeltasRejected=" << tileDeltasRejected
        << ",drawCommandsBuilt=" << drawCommandsBuilt;
    return out.str();
}

FramePipeline::FramePipeline(std::unique_ptr<ComputeBackend> backend): backend{std::move(backend)}
{
    compiledFormula = formulaCompiler.compile(appState.formulaExpression);
    if (compiledFormula && compiledFormula->diagnostics.ok)
    {
        ++pipelineCounters.formulasCompiled;
    }
}

FrameSnapshot FramePipeline::process(const InputEvent &event)
{
    ++frameId;
    const auto diff = reducer.reduce(appState, event);
    const auto effects = effectPlanner.plan(diff);

    if (effects.compileFormula)
    {
        auto nextFormula = formulaCompiler.compile(appState.formulaExpression);
        if (nextFormula.diagnostics.ok)
        {
            if (!compiledFormula
                || compiledFormula->handle.semanticsHash != nextFormula.handle.semanticsHash
                || compiledFormula->handle.sourceHash != nextFormula.handle.sourceHash)
            {
                ++pipelineCounters.formulasCompiled;
            }
            compiledFormula = std::move(nextFormula);
        }
    }

    FrameSnapshot snapshot;
    snapshot.frameId = frameId;
    snapshot.appStateDiff = diff.debugString;
    if (compiledFormula)
    {
        snapshot.formula = compiledFormula->handle;
    }

    if (!compiledFormula || !compiledFormula->diagnostics.ok)
    {
        snapshot.counters = pipelineCounters.toDebugString();
        return snapshot;
    }

    const auto request = makeViewportRequest(diff);
    snapshot.viewportRequest = request;

    std::vector<TileJob> jobs;
    const auto shouldRequestTiles = effects.requestTiles || !hasRequestedTiles;
    if (shouldRequestTiles)
    {
        jobs = scheduler.buildJobs(request, tileCache, SchedulerBudget{});
        pipelineCounters.tileJobsScheduled += jobs.size();

        auto transaction = executeVisibleJobs(request, jobs);
        const auto applyResult = tileCache.apply(transaction);
        pipelineCounters.tileDeltasApplied += applyResult.applied;
        pipelineCounters.tileDeltasRejected += applyResult.rejected;
        snapshot.appliedTransactions.push_back(std::move(transaction));
        hasRequestedTiles = true;
    }

    snapshot.schedulerSummary = "jobs=" + std::to_string(jobs.size());
    snapshot.visibleCover = TileCoverageIndex{tileCache}.visibleCover(request);

    const auto records = tileCache.recordsForFormula(request.formula.semanticsHash);
    snapshot.uploadPlan = uploadPlanner.plan(records, UploadBudget{});

    auto commands = buildCommands(snapshot.visibleCover, request);
    snapshot.drawCommands.assign(commands.commands().begin(), commands.commands().end());
    pipelineCounters.drawCommandsBuilt += snapshot.drawCommands.size();
    snapshot.counters = pipelineCounters.toDebugString();

    return snapshot;
}

const AppState &FramePipeline::state() const
{
    return appState;
}

const TileCache &FramePipeline::tiles() const
{
    return tileCache;
}

const FramePipelineCounters &FramePipeline::counters() const
{
    return pipelineCounters;
}

RenderResourceManager &FramePipeline::renderResources()
{
    return resources;
}

ViewportRequest FramePipeline::makeViewportRequest(const StateDiff &diff) const
{
    return {
        .header = {
            .requestId = diff.requestId,
            .generation = diff.generation
        },
        .formula = compiledFormula->handle,
        .xRange = appState.xRange,
        .yRange = appState.yRange,
        .framebufferWidth = appState.framebufferWidth,
        .framebufferHeight = appState.framebufferHeight,
        .devicePixelRatio = 1.0
    };
}

TileTransaction FramePipeline::executeVisibleJobs(const ViewportRequest &request,
                                                  const std::vector<TileJob> &jobs)
{
    TileTransaction transaction{
        .header = request.header,
        .semanticsHash = request.formula.semanticsHash
    };

    std::vector<TileKey> classifyKeys;
    std::vector<TileKey> rasterKeys;
    for (const auto &job : jobs)
    {
        if (job.kind == JobKind::ClassifyInterval)
        {
            classifyKeys.push_back(job.key);
        }
        else if (job.kind == JobKind::RasterizeRegion)
        {
            rasterKeys.push_back(job.key);
        }
    }

    std::vector<double> xMin;
    std::vector<double> xMax;
    std::vector<double> yMin;
    std::vector<double> yMax;
    xMin.reserve(classifyKeys.size());
    xMax.reserve(classifyKeys.size());
    yMin.reserve(classifyKeys.size());
    yMax.reserve(classifyKeys.size());

    for (const auto &key : classifyKeys)
    {
        const auto bounds = tileBounds(key);
        xMin.push_back(bounds.xMin);
        xMax.push_back(bounds.xMax);
        yMin.push_back(bounds.yMin);
        yMax.push_back(bounds.yMax);

        transaction.deltas.push_back({
            .header = request.header,
            .semanticsHash = request.formula.semanticsHash,
            .key = key,
            .stage = TileStage::IntervalQueued,
            .classification = TileClassification::Unknown
        });
    }

    std::vector<TileClassificationResult> classifications(classifyKeys.size());
    const auto batchResult = backend->classifyIntervals(
        IntervalBatchView{compiledFormula ? &*compiledFormula : nullptr, classifyKeys, xMin, xMax, yMin, yMax},
        classifications);

    if (!batchResult.ok)
    {
        return transaction;
    }

    std::vector<TileKey> mixedRasterKeys = rasterKeys;
    for (const auto &classification : classifications)
    {
        if (classification.classification == TileClassification::Mixed)
        {
            mixedRasterKeys.push_back(classification.key);
        }
    }

    std::vector<double> rasterXMin;
    std::vector<double> rasterXMax;
    std::vector<double> rasterYMin;
    std::vector<double> rasterYMax;
    std::vector<uint32_t> rasterOffsets;
    rasterXMin.reserve(mixedRasterKeys.size());
    rasterXMax.reserve(mixedRasterKeys.size());
    rasterYMin.reserve(mixedRasterKeys.size());
    rasterYMax.reserve(mixedRasterKeys.size());
    rasterOffsets.reserve(mixedRasterKeys.size());
    for (const auto &key : mixedRasterKeys)
    {
        const auto bounds = tileBounds(key);
        rasterXMin.push_back(bounds.xMin);
        rasterXMax.push_back(bounds.xMax);
        rasterYMin.push_back(bounds.yMin);
        rasterYMax.push_back(bounds.yMax);
        rasterOffsets.push_back(static_cast<uint32_t>(rasterOffsets.size() * MinChunkPixels * MinChunkPixels));
    }

    std::unordered_map<TileKey, RegionImageRef, TileKeyHash> regionRefs;
    if (!mixedRasterKeys.empty())
    {
        std::vector<RegionOutput> rasterOutputs(mixedRasterKeys.size());
        const auto rasterResult = backend->rasterizeRegions(
            RasterBatchView{
                compiledFormula ? &*compiledFormula : nullptr,
                mixedRasterKeys,
                rasterXMin,
                rasterXMax,
                rasterYMin,
                rasterYMax,
                rasterOffsets,
                MinChunkPixels
            },
            rasterOutputs);

        if (rasterResult.ok)
        {
            for (auto &output : rasterOutputs)
            {
                const auto payloadId = nextRegionPayloadId++;
                const auto width = static_cast<int>(output.width);
                const auto height = static_cast<int>(output.height);
                const auto key = output.key;
                regionPayloads[payloadId] = std::move(output);
                regionRefs[key] = RegionImageRef{payloadId, width, height};
            }
        }
    }

    for (const auto &classification : classifications)
    {
        transaction.deltas.push_back({
            .header = request.header,
            .semanticsHash = request.formula.semanticsHash,
            .key = classification.key,
            .stage = TileStage::IntervalReady,
            .classification = classification.classification,
            .interval = classification.interval
        });

        auto finalStage = TileStage::MixedNeedsRegion;
        if (classification.classification == TileClassification::UniformTrue)
        {
            finalStage = TileStage::UniformTrue;
        }
        else if (classification.classification == TileClassification::UniformFalse)
        {
            finalStage = TileStage::UniformFalse;
        }

        transaction.deltas.push_back({
            .header = request.header,
            .semanticsHash = request.formula.semanticsHash,
            .key = classification.key,
            .stage = finalStage,
            .classification = classification.classification,
            .interval = classification.interval
        });

        if (classification.classification == TileClassification::Mixed)
        {
            if (const auto regionIt = regionRefs.find(classification.key); regionIt != regionRefs.end())
            {
                transaction.deltas.push_back({
                    .header = request.header,
                    .semanticsHash = request.formula.semanticsHash,
                    .key = classification.key,
                    .stage = TileStage::RegionQueued,
                    .classification = classification.classification
                });
                transaction.deltas.push_back({
                    .header = request.header,
                    .semanticsHash = request.formula.semanticsHash,
                    .key = classification.key,
                    .stage = TileStage::RegionReady,
                    .classification = classification.classification,
                    .region = regionIt->second
                });
            }
        }
    }

    for (const auto &key : rasterKeys)
    {
        const auto regionIt = regionRefs.find(key);
        if (regionIt == regionRefs.end())
        {
            continue;
        }
        transaction.deltas.push_back({
            .header = request.header,
            .semanticsHash = request.formula.semanticsHash,
            .key = key,
            .stage = TileStage::RegionQueued,
            .classification = TileClassification::Mixed
        });
        transaction.deltas.push_back({
            .header = request.header,
            .semanticsHash = request.formula.semanticsHash,
            .key = key,
            .stage = TileStage::RegionReady,
            .classification = TileClassification::Mixed,
            .region = regionIt->second
        });
    }

    return transaction;
}

FrameCommandBuffer FramePipeline::buildCommands(const std::vector<TileKey> &visibleCover,
                                                const ViewportRequest &request)
{
    resources.setPlotViewport(request.xRange, request.yRange);

    std::vector<RenderTileInstance> instances;
    instances.reserve(visibleCover.size());
    for (const auto &key : visibleCover)
    {
        auto visualState = TileVisualState::Missing;
        TextureSlice regionSlice{};
        if (const auto *record = tileCache.find(key, request.formula.semanticsHash))
        {
            switch (record->classification)
            {
            case TileClassification::UniformTrue:
                visualState = TileVisualState::UniformTrue;
                break;
            case TileClassification::UniformFalse:
                visualState = TileVisualState::UniformFalse;
                break;
            case TileClassification::Mixed:
                visualState = TileVisualState::MixedRegion;
                break;
            case TileClassification::Unknown:
            default:
                visualState = TileVisualState::Missing;
                break;
            }

            if (record->regionPixels)
            {
                if (const auto payloadIt = regionPayloads.find(record->regionPixels->id);
                    payloadIt != regionPayloads.end())
                {
                    regionSlice = resources.registerRegionImage(*record->regionPixels, payloadIt->second.pixels);
                }
            }
        }

        instances.push_back({
            .key = key,
            .worldBounds = tileBounds(key),
            .visualState = visualState,
            .regionSlice = regionSlice
        });
    }
    resources.setPlotInstances(std::move(instances));

    FrameCommandBuffer buffer;
    if (!visibleCover.empty())
    {
        buffer.add({
            .layer = RenderLayer::Plot,
            .pipeline = resources.plotPipeline(),
            .geometry = resources.staticQuadGeometry(),
            .material = resources.plotMaterial(),
            .textures = resources.regionTextureSet(),
            .instanceRange = BufferRange{1, 0, static_cast<uint32_t>(visibleCover.size())},
            .sortKey = 0
        });
    }
    buffer.freezeAndSort();
    return buffer;
}
}
