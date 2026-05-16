#include "FramePipeline.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "../Tile/TileMath.h"

namespace
{
using GlyphRows = std::array<const char *, 7>;

GlyphRows glyphFor(const char input)
{
    const auto c = static_cast<char>(std::tolower(static_cast<unsigned char>(input)));
    switch (c)
    {
    case '0': return {"111", "101", "101", "101", "101", "101", "111"};
    case '1': return {"010", "110", "010", "010", "010", "010", "111"};
    case '2': return {"111", "001", "001", "111", "100", "100", "111"};
    case '3': return {"111", "001", "001", "111", "001", "001", "111"};
    case '4': return {"101", "101", "101", "111", "001", "001", "001"};
    case '5': return {"111", "100", "100", "111", "001", "001", "111"};
    case '6': return {"111", "100", "100", "111", "101", "101", "111"};
    case '7': return {"111", "001", "001", "010", "010", "100", "100"};
    case '8': return {"111", "101", "101", "111", "101", "101", "111"};
    case '9': return {"111", "101", "101", "111", "001", "001", "111"};
    case 'a': return {"010", "101", "101", "111", "101", "101", "101"};
    case 'b': return {"110", "101", "101", "110", "101", "101", "110"};
    case 'c': return {"111", "100", "100", "100", "100", "100", "111"};
    case 'd': return {"110", "101", "101", "101", "101", "101", "110"};
    case 'e': return {"111", "100", "100", "111", "100", "100", "111"};
    case 'f': return {"111", "100", "100", "111", "100", "100", "100"};
    case 'g': return {"111", "100", "100", "101", "101", "101", "111"};
    case 'h': return {"101", "101", "101", "111", "101", "101", "101"};
    case 'i': return {"111", "010", "010", "010", "010", "010", "111"};
    case 'j': return {"001", "001", "001", "001", "101", "101", "111"};
    case 'k': return {"101", "101", "110", "100", "110", "101", "101"};
    case 'l': return {"100", "100", "100", "100", "100", "100", "111"};
    case 'm': return {"101", "111", "111", "101", "101", "101", "101"};
    case 'n': return {"101", "111", "111", "111", "101", "101", "101"};
    case 'o': return {"111", "101", "101", "101", "101", "101", "111"};
    case 'p': return {"111", "101", "101", "111", "100", "100", "100"};
    case 'q': return {"111", "101", "101", "101", "111", "001", "001"};
    case 'r': return {"110", "101", "101", "110", "101", "101", "101"};
    case 's': return {"111", "100", "100", "111", "001", "001", "111"};
    case 't': return {"111", "010", "010", "010", "010", "010", "010"};
    case 'u': return {"101", "101", "101", "101", "101", "101", "111"};
    case 'v': return {"101", "101", "101", "101", "101", "101", "010"};
    case 'w': return {"101", "101", "101", "101", "111", "111", "101"};
    case 'x': return {"101", "101", "010", "010", "010", "101", "101"};
    case 'y': return {"101", "101", "101", "010", "010", "010", "010"};
    case 'z': return {"111", "001", "001", "010", "100", "100", "111"};
    case '+': return {"000", "010", "010", "111", "010", "010", "000"};
    case '-': return {"000", "000", "000", "111", "000", "000", "000"};
    case '*': return {"000", "101", "010", "111", "010", "101", "000"};
    case '/': return {"001", "001", "001", "010", "100", "100", "100"};
    case '^': return {"010", "101", "000", "000", "000", "000", "000"};
    case '<': return {"001", "010", "100", "100", "100", "010", "001"};
    case '>': return {"100", "010", "001", "001", "001", "010", "100"};
    case '=': return {"000", "000", "111", "000", "111", "000", "000"};
    case '(': return {"001", "010", "100", "100", "100", "010", "001"};
    case ')': return {"100", "010", "001", "001", "001", "010", "100"};
    case '.': return {"000", "000", "000", "000", "000", "110", "110"};
    case ',': return {"000", "000", "000", "000", "010", "010", "100"};
    case ':': return {"000", "010", "010", "000", "010", "010", "000"};
    case '|': return {"010", "010", "010", "010", "010", "010", "010"};
    case ' ': return {"000", "000", "000", "000", "000", "000", "000"};
    default: return {"111", "001", "010", "010", "000", "010", "000"};
    }
}

void appendTextRects(std::vector<gx::OverlayRect> &rects,
                     const std::string_view text,
                     const float x,
                     const float y,
                     const float pixel,
                     const std::array<float, 4> &color)
{
    auto penX = x;
    constexpr auto glyphWidth = 3;
    constexpr auto glyphHeight = 7;
    for (const auto c : text)
    {
        const auto rows = glyphFor(c);
        for (auto row = 0; row < glyphHeight; ++row)
        {
            for (auto col = 0; col < glyphWidth; ++col)
            {
                if (rows[row][col] != '1')
                {
                    continue;
                }
                const auto xMin = penX + static_cast<float>(col) * pixel;
                const auto yMax = y - static_cast<float>(row) * pixel;
                rects.push_back({xMin, xMin + pixel * 0.82f, yMax - pixel * 0.82f, yMax, color});
            }
        }
        penX += static_cast<float>(glyphWidth + 1) * pixel;
    }
}

double pickNiceStep(const double span, const int desiredTickCount)
{
    if (span <= 0.0 || desiredTickCount <= 0)
    {
        return 1.0;
    }

    const auto roughStep = span / static_cast<double>(desiredTickCount);
    const auto power = std::pow(10.0, std::floor(std::log10(roughStep)));
    const auto normalized = roughStep / power;
    auto niceNormalized = 10.0;
    if (normalized <= 1.0)
    {
        niceNormalized = 1.0;
    }
    else if (normalized <= 2.0)
    {
        niceNormalized = 2.0;
    }
    else if (normalized <= 5.0)
    {
        niceNormalized = 5.0;
    }
    return niceNormalized * power;
}

double firstTickAtOrAbove(const double lowerBound, const double step)
{
    constexpr auto epsilon = 1e-9;
    return std::ceil((lowerBound - epsilon) / step) * step;
}

float toNdc(const double value, const Interval &range)
{
    return static_cast<float>(((value - range.lower) / range.size() - 0.5) * 2.0);
}

std::string formatTick(const double value)
{
    std::ostringstream out;
    out << std::setprecision(4) << std::defaultfloat << value;
    return out.str();
}

gx::TileVisualState normalVisualStateForRecord(const gx::TileRecord *record)
{
    if (!record)
    {
        return gx::TileVisualState::Missing;
    }

    switch (record->classification)
    {
    case gx::TileClassification::UniformTrue:
        return gx::TileVisualState::UniformTrue;
    case gx::TileClassification::UniformFalse:
        return gx::TileVisualState::UniformFalse;
    case gx::TileClassification::Mixed:
        return gx::TileVisualState::MixedRegion;
    case gx::TileClassification::Unknown:
    default:
        return gx::TileVisualState::Missing;
    }
}

gx::TileVisualState debugVisualStateForRecord(const gx::TileRecord *record)
{
    if (!record)
    {
        return gx::TileVisualState::DebugMissing;
    }

    switch (record->classification)
    {
    case gx::TileClassification::Mixed:
        return gx::TileVisualState::DebugMixed;
    case gx::TileClassification::UniformTrue:
    case gx::TileClassification::UniformFalse:
        return gx::TileVisualState::DebugUniform;
    case gx::TileClassification::Unknown:
    default:
        return gx::TileVisualState::DebugMissing;
    }
}
}

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
    if (effects.requestTiles || !hasRequestedTiles || std::holds_alternative<RenderTickEvent>(event))
    {
        jobs = scheduler.buildJobs(request, tileCache, SchedulerBudget{});
        pipelineCounters.tileJobsScheduled += jobs.size();

        if (!jobs.empty())
        {
            auto transaction = executeVisibleJobs(request, jobs);
            const auto applyResult = tileCache.apply(transaction);
            pipelineCounters.tileDeltasApplied += applyResult.applied;
            pipelineCounters.tileDeltasRejected += applyResult.rejected;
            snapshot.appliedTransactions.push_back(std::move(transaction));
        }
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
    const auto rasterLeafLevel = leafTileLevel(request);
    for (const auto &classification : classifications)
    {
        if (classification.classification == TileClassification::Mixed
            && classification.key.level <= rasterLeafLevel)
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
        rasterOffsets.push_back(static_cast<uint32_t>(rasterOffsets.size() * RasterTexturePixels * RasterTexturePixels));
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
                RasterTexturePixels
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
    resources.setGridState(request.xRange, request.yRange, request.framebufferWidth, request.framebufferHeight);

    std::vector<RenderTileInstance> instances;
    instances.reserve(visibleCover.size());
    std::vector<RenderTileInstance> debugInstances;
    debugInstances.reserve(appState.debug ? visibleCover.size() : 0);
    for (const auto &key : visibleCover)
    {
        TextureSlice regionSlice{};
        const auto *record = tileCache.find(key, request.formula.semanticsHash);
        if (record)
        {
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
            .visualState = normalVisualStateForRecord(record),
            .regionSlice = regionSlice
        });
        if (appState.debug)
        {
            debugInstances.push_back({
                .key = key,
                .worldBounds = tileBounds(key),
                .visualState = debugVisualStateForRecord(record)
            });
        }
    }

    const auto plotInstanceCount = instances.size();
    resources.setPlotInstances(std::move(instances));
    const auto debugPlotInstanceCount = debugInstances.size();
    resources.setDebugPlotInstances(std::move(debugInstances));
    resources.setOverlayRects(buildOverlayRects());

    FrameCommandBuffer buffer;
    buffer.add({
        .layer = RenderLayer::Grid,
        .pipeline = resources.gridPipeline(),
        .geometry = resources.staticQuadGeometry(),
        .material = resources.gridMaterial(),
        .sortKey = 10
    });
    if (!visibleCover.empty())
    {
        buffer.add({
            .layer = RenderLayer::Plot,
            .pipeline = resources.plotPipeline(),
            .geometry = resources.staticQuadGeometry(),
            .material = resources.plotMaterial(),
            .textures = resources.regionTextureSet(),
            .instanceRange = BufferRange{1, 0, static_cast<uint32_t>(plotInstanceCount)},
            .sortKey = 0
        });
    }
    if (debugPlotInstanceCount > 0)
    {
        buffer.add({
            .layer = RenderLayer::Contour,
            .pipeline = resources.debugPlotPipeline(),
            .geometry = resources.staticQuadGeometry(),
            .material = resources.plotMaterial(),
            .instanceRange = BufferRange{3, 0, static_cast<uint32_t>(debugPlotInstanceCount)},
            .sortKey = 0
        });
    }
    if (resources.overlayRectCount() > 0)
    {
        buffer.add({
            .layer = RenderLayer::Text,
            .pipeline = resources.overlayPipeline(),
            .geometry = resources.staticQuadGeometry(),
            .material = resources.overlayMaterial(),
            .instanceRange = BufferRange{2, 0, static_cast<uint32_t>(resources.overlayRectCount())},
            .sortKey = 0
        });
    }
    buffer.freezeAndSort();
    return buffer;
}

std::vector<OverlayRect> FramePipeline::buildOverlayRects() const
{
    std::vector<OverlayRect> rects;
    const std::array white{0.92f, 0.96f, 1.0f, 1.0f};
    const std::array muted{0.78f, 0.84f, 0.92f, 0.9f};
    const std::array dark{0.06f, 0.07f, 0.09f, 0.88f};
    const std::array border{0.70f, 0.76f, 0.84f, 0.85f};
    const std::array label{0.80f, 0.84f, 0.90f, 0.74f};

    const auto xAxisY = std::clamp(toNdc(0.0, appState.yRange), -0.93f, 0.93f);
    const auto yAxisX = std::clamp(toNdc(0.0, appState.xRange), -0.93f, 0.93f);
    const auto xStep = pickNiceStep(appState.xRange.size(), 8);
    const auto yStep = pickNiceStep(appState.yRange.size(), 8);
    constexpr auto loopEpsilon = 1e-9;
    auto tickCount = 0;
    for (auto x = firstTickAtOrAbove(appState.xRange.lower, xStep);
         x <= appState.xRange.upper + loopEpsilon && tickCount < 24;
         x += xStep, ++tickCount)
    {
        if (std::abs(x) < 1e-9)
        {
            continue;
        }
        appendTextRects(rects, formatTick(x), toNdc(x, appState.xRange) - 0.035f, xAxisY - 0.035f, 0.006f, label);
    }
    tickCount = 0;
    for (auto y = firstTickAtOrAbove(appState.yRange.lower, yStep);
         y <= appState.yRange.upper + loopEpsilon && tickCount < 24;
         y += yStep, ++tickCount)
    {
        if (std::abs(y) < 1e-9)
        {
            continue;
        }
        appendTextRects(rects, formatTick(y), yAxisX + 0.012f, toNdc(y, appState.yRange) + 0.020f, 0.006f, label);
    }

    if (appState.formulaInput.active)
    {
        rects.push_back({-0.96f, 0.96f, 0.80f, 0.97f, dark});
        rects.push_back({-0.96f, 0.96f, 0.965f, 0.97f, border});
        rects.push_back({-0.96f, 0.96f, 0.80f, 0.805f, border});
        rects.push_back({-0.96f, -0.955f, 0.80f, 0.97f, border});
        rects.push_back({0.955f, 0.96f, 0.80f, 0.97f, border});
        appendTextRects(rects, "f(x,y)=", -0.92f, 0.925f, 0.012f, muted);
        appendTextRects(rects, appState.formulaInput.buffer, -0.70f, 0.925f, 0.012f, white);

        const auto cursorX = -0.70f + static_cast<float>(appState.formulaInput.cursor) * 0.048f;
        rects.push_back({cursorX, cursorX + 0.006f, 0.825f, 0.925f, white});
    }

    if (appState.debug)
    {
        rects.push_back({-0.98f, -0.34f, -0.98f, -0.72f, dark});
        rects.push_back({-0.98f, -0.34f, -0.725f, -0.72f, border});
        appendTextRects(rects, "debug frames", -0.95f, -0.76f, 0.007f, white);
        appendTextRects(rects,
                        "compiled:" + std::to_string(pipelineCounters.formulasCompiled),
                        -0.95f,
                        -0.82f,
                        0.006f,
                        muted);
        appendTextRects(rects,
                        "jobs:" + std::to_string(pipelineCounters.tileJobsScheduled),
                        -0.95f,
                        -0.87f,
                        0.006f,
                        muted);
        appendTextRects(rects,
                        "applied:" + std::to_string(pipelineCounters.tileDeltasApplied)
                            + "/" + std::to_string(pipelineCounters.tileDeltasRejected),
                        -0.95f,
                        -0.92f,
                        0.006f,
                        muted);
    }

    return rects;
}
}
