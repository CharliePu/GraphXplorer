#include "FramePipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

#include "../Tile/TileMath.h"
#include "../Util/PipelineLog.h"
#include "UiLayout.h"

namespace
{
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

float clampTextX(const float x, const std::string_view text, const float pixelHeight, const int framebufferWidth)
{
    const auto width = gx::textAdvanceNdc(text.size(), pixelHeight, framebufferWidth);
    return std::clamp(x, -0.995f, std::max(-0.995f, 0.995f - width));
}

float clampTextY(const float y, const float pixelHeight, const int framebufferHeight)
{
    const auto height = gx::textHeightNdc(pixelHeight, framebufferHeight);
    return std::clamp(y, std::min(0.995f, -0.995f + height), 0.995f);
}

struct DebugOverlayLayout
{
    float panelLeft{-0.98f};
    float panelRight{-0.34f};
    float panelBottom{-0.98f};
    float panelTop{-0.72f};
    float textX{-0.95f};
    std::array<float, 6> textY{-0.76f, -0.81f, -0.86f, -0.91f, -0.95f, -0.98f};
    std::array<float, 6> pixelHeights{12.0f, 12.0f, 12.0f, 12.0f, 12.0f, 12.0f};
};

std::array<std::string, 6> debugOverlayLines(const gx::FramePipelineDebugStats &stats,
                                             const gx::FramePipelineCounters &counters)
{
    const auto processingTiles = stats.queuedIntervalTiles + stats.queuedRegionTiles;
    const auto stuckTiles = stats.stuckIntervalTiles + stats.stuckRegionTiles;
    return {
        "tiles:" + std::to_string(stats.displayTiles)
            + " plot:" + std::to_string(stats.plotTiles),
        "uniform:" + std::to_string(stats.uniformTiles)
            + " mixed:" + std::to_string(stats.mixedTiles)
            + " missing:" + std::to_string(stats.missingTiles),
        "fallback:" + std::to_string(stats.fallbackTiles)
            + " clipped:" + std::to_string(stats.clippedFallbackTiles),
        "processing:" + std::to_string(processingTiles)
            + " running:" + std::to_string(stats.inFlightJobs)
            + " done:" + std::to_string(stats.completedJobs),
        "queued i:" + std::to_string(stats.queuedIntervalTiles)
            + " r:" + std::to_string(stats.queuedRegionTiles)
            + " jobs:" + std::to_string(stats.submittedJobs),
        "stuck:" + std::to_string(stuckTiles)
            + " applied:" + std::to_string(counters.tileDeltasApplied)
            + "/" + std::to_string(counters.tileDeltasRejected)
    };
}

DebugOverlayLayout debugOverlayLayoutFor(const gx::FramePipelineDebugStats &stats,
                                         const gx::FramePipelineCounters &counters,
                                         const int framebufferWidth,
                                         const int framebufferHeight,
                                         const float scale)
{
    const auto width = std::max(1, framebufferWidth);
    const auto height = std::max(1, framebufferHeight);
    const auto lines = debugOverlayLines(stats, counters);
    std::array<float, 6> pixelHeights{};
    pixelHeights.fill(12.0f * scale);
    auto textWidthPx = 0.0f;
    for (size_t index = 0; index < lines.size(); ++index)
    {
        textWidthPx = std::max(textWidthPx, gx::textAdvancePx(lines[index].size(), pixelHeights[index]));
    }

    const auto logicalMin = static_cast<float>(std::min(width, height)) / scale;
    const auto marginPx = std::clamp(logicalMin * 0.020f, 8.0f, 14.0f) * scale;
    const auto paddingX = 10.0f * scale;
    const auto paddingY = 8.0f * scale;
    const auto lineGapPx = 16.0f * scale;
    const auto panelWidthPx = std::min(static_cast<float>(width) - marginPx * 2.0f, textWidthPx + paddingX * 2.0f);
    const auto desiredHeightPx = paddingY * 2.0f
        + static_cast<float>(lines.size() - 1) * lineGapPx
        + 12.0f * scale;
    const auto panelHeightPx = std::min(static_cast<float>(height) - marginPx * 2.0f, desiredHeightPx);
    const auto panelLeftPx = marginPx;
    const auto panelRightPx = panelLeftPx + std::max(48.0f * scale, panelWidthPx);
    const auto panelBottomPx = static_cast<float>(height) - marginPx;
    const auto panelTopPx = std::max(marginPx, panelBottomPx - panelHeightPx);
    const auto textXPx = panelLeftPx + paddingX;
    std::array<float, 6> textYPx{};
    for (size_t index = 0; index < textYPx.size(); ++index)
    {
        textYPx[index] = panelTopPx + paddingY + static_cast<float>(index) * lineGapPx;
    }

    return {
        .panelLeft = gx::normalizedPixelX(panelLeftPx, width),
        .panelRight = gx::normalizedPixelX(panelRightPx, width),
        .panelBottom = gx::normalizedPixelYFromTop(panelBottomPx, height),
        .panelTop = gx::normalizedPixelYFromTop(panelTopPx, height),
        .textX = gx::normalizedPixelX(textXPx, width),
        .textY = {
            gx::normalizedPixelYFromTop(textYPx[0], height),
            gx::normalizedPixelYFromTop(textYPx[1], height),
            gx::normalizedPixelYFromTop(textYPx[2], height),
            gx::normalizedPixelYFromTop(textYPx[3], height),
            gx::normalizedPixelYFromTop(textYPx[4], height),
            gx::normalizedPixelYFromTop(textYPx[5], height)
        },
        .pixelHeights = pixelHeights
    };
}

int desiredTickCountForAxis(const int pixels, const int minimumSpacingPx)
{
    return std::clamp(pixels / std::max(1, minimumSpacingPx), 2, 12);
}

std::string formatTick(const double value)
{
    std::ostringstream out;
    out << std::setprecision(4) << std::defaultfloat << value;
    return out.str();
}

std::optional<gx::RenderTileInstance> normalPlotInstanceForDisplayTile(const gx::DisplayTile &tile,
                                                                       const gx::TextureSlice &regionSlice)
{
    auto visualState = tile.visualState;
    if (visualState == gx::TileVisualState::MixedRegion && regionSlice.textureId == 0)
    {
        visualState = gx::TileVisualState::Missing;
    }
    if (visualState == gx::TileVisualState::UniformFalse)
    {
        return std::nullopt;
    }
    return gx::RenderTileInstance{
        .key = tile.sourceKey,
        .worldBounds = tile.worldBounds,
        .visualState = visualState,
        .regionSlice = regionSlice,
        .uvRect = tile.uvRect
    };
}

bool isPresentableDisplayTile(const gx::DisplayTile &tile)
{
    switch (tile.visualState)
    {
    case gx::TileVisualState::UniformTrue:
    case gx::TileVisualState::UniformFalse:
        return true;
    case gx::TileVisualState::MixedRegion:
        return tile.cpuRegion.has_value() && tile.gpuSlice.textureId != 0;
    default:
        return false;
    }
}

std::vector<gx::DisplayTile> presentableDisplayTiles(std::span<const gx::DisplayTile> tiles)
{
    std::vector<gx::DisplayTile> result;
    result.reserve(tiles.size());
    for (const auto &tile : tiles)
    {
        if (isPresentableDisplayTile(tile))
        {
            result.push_back(tile);
        }
    }
    return result;
}
}

namespace gx
{
namespace
{
bool containsTileKey(const std::vector<TileKey> &keys, const TileKey &candidate)
{
    return std::ranges::find(keys, candidate) != keys.end();
}
}

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

FramePipeline::FramePipeline(std::unique_ptr<ComputeBackend> backend): tileRuntime{std::move(backend)}
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
    std::optional<InputEvent> substitutedEvent;
    if (std::holds_alternative<SubmitFormulaInputEvent>(event) && appState.formulaInput.active)
    {
        if (appState.formulaInput.buffer.empty())
        {
            substitutedEvent = RejectFormulaInputEvent{"Expression cannot be empty"};
        }
        else
        {
            auto candidate = formulaCompiler.compile(appState.formulaInput.buffer);
            if (!candidate.diagnostics.ok)
            {
                substitutedEvent = RejectFormulaInputEvent{candidate.diagnostics.message};
            }
        }
    }

    const auto &effectiveEvent = substitutedEvent ? *substitutedEvent : event;
    const auto diff = reducer.reduce(appState, effectiveEvent);
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

    tileRuntime.setLatestRequest(request, *compiledFormula);
    const auto drainResult = tileRuntime.drainCompleted(tileCache, regionPayloads);
    pipelineCounters.tileDeltasApplied += drainResult.applied;
    pipelineCounters.tileDeltasRejected += drainResult.rejected;
    snapshot.appliedTransactions = drainResult.transactions;

    const auto tilePlan = tilePlanner.plan(request, tileCache, TilePlanBudget{}, 4);
    tileRuntime.submitJobs(tilePlan.jobs);
    pipelineCounters.tileJobsScheduled += tilePlan.jobs.size();
    hasRequestedTiles = true;

    auto tileDebugCounts = tileCache.debugCountsForFormula(request.formula.semanticsHash);
    const auto inFlightCount = tileRuntime.inFlightCount();
    const auto pendingCompletionCount = tileRuntime.pendingCompletionCount();
    if (inFlightCount == 0 && pendingCompletionCount == 0 && tilePlan.jobs.empty())
    {
        tileDebugCounts.stuckIntervalQueued = tileDebugCounts.intervalQueued;
        tileDebugCounts.stuckRegionQueued = tileDebugCounts.regionQueued;
    }
    snapshot.schedulerSummary = "inFlight=" + std::to_string(inFlightCount)
        + ",completed=" + std::to_string(pendingCompletionCount)
        + ",queuedInterval=" + std::to_string(tileDebugCounts.intervalQueued)
        + ",queuedRegion=" + std::to_string(tileDebugCounts.regionQueued)
        + ",stuckInterval=" + std::to_string(tileDebugCounts.stuckIntervalQueued)
        + ",stuckRegion=" + std::to_string(tileDebugCounts.stuckRegionQueued);
    const auto *previousFrame = committedVisualFrame
        && committedVisualFrame->semantics == request.formula.semanticsHash
        ? &*committedVisualFrame
        : nullptr;
    auto visualFrame = visualCoverBuilder.build(request, tileCache, previousFrame, 4);
    snapshot.displayTiles = std::move(visualFrame.tiles);
    snapshot.visibleCover.reserve(snapshot.displayTiles.size());
    std::vector<RegionImageRef> visibleRegions;
    visibleRegions.reserve(snapshot.displayTiles.size());
    debugStats = FramePipelineDebugStats{
        .displayTiles = snapshot.displayTiles.size(),
        .inFlightJobs = inFlightCount,
        .completedJobs = pendingCompletionCount,
        .queuedIntervalTiles = tileDebugCounts.intervalQueued,
        .queuedRegionTiles = tileDebugCounts.regionQueued,
        .stuckIntervalTiles = tileDebugCounts.stuckIntervalQueued,
        .stuckRegionTiles = tileDebugCounts.stuckRegionQueued,
        .submittedJobs = tilePlan.jobs.size()
    };
    for (auto &tile : snapshot.displayTiles)
    {
        snapshot.visibleCover.push_back(tile.desiredKey);
        if (tile.visualState != TileVisualState::UniformFalse)
        {
            ++debugStats.plotTiles;
        }
        switch (tile.visualState)
        {
        case TileVisualState::Missing:
            ++debugStats.missingTiles;
            break;
        case TileVisualState::MixedRegion:
            ++debugStats.mixedTiles;
            break;
        case TileVisualState::UniformFalse:
        case TileVisualState::UniformTrue:
            ++debugStats.uniformTiles;
            break;
        default:
            break;
        }
        if (tile.isFallback)
        {
            ++debugStats.fallbackTiles;
        }
        if (tile.clippedFallback)
        {
            ++debugStats.clippedFallbackTiles;
        }
        if (tile.cpuRegion)
        {
            tile.gpuSlice = resources.findRegionImage(*tile.cpuRegion);
            visibleRegions.push_back(*tile.cpuRegion);
        }
    }
    if (appState.debug
        && (frameId % 30 == 0
            || debugStats.submittedJobs > 0
            || drainResult.applied > 0
            || drainResult.rejected > 0))
    {
        PipelineLog::log(
            "frame=%llu view=[%.3f,%.3f]x[%.3f,%.3f] root=%d leaf=%d "
            "tiles=%zu plot=%zu uniform=%zu mixed=%zu missing=%zu fallback=%zu clipped=%zu "
            "inFlight=%zu completed=%zu queuedI=%zu queuedR=%zu stuckI=%zu stuckR=%zu jobs=%zu applied=%zu rejected=%zu",
            static_cast<unsigned long long>(frameId),
            request.xRange.lower,
            request.xRange.upper,
            request.yRange.lower,
            request.yRange.upper,
            rootTileLevel(request),
            leafTileLevel(request),
            debugStats.displayTiles,
            debugStats.plotTiles,
            debugStats.uniformTiles,
            debugStats.mixedTiles,
            debugStats.missingTiles,
            debugStats.fallbackTiles,
            debugStats.clippedFallbackTiles,
            debugStats.inFlightJobs,
            debugStats.completedJobs,
            debugStats.queuedIntervalTiles,
            debugStats.queuedRegionTiles,
            debugStats.stuckIntervalTiles,
            debugStats.stuckRegionTiles,
            debugStats.submittedJobs,
            drainResult.applied,
            drainResult.rejected);
    }
    resources.beginRegionFrame(visibleRegions);
    snapshot.uploadPlan = uploadPlanner.planVisible(snapshot.displayTiles, UploadBudget{});

    auto commands = buildCommands(snapshot.displayTiles, request, snapshot.uploadPlan);
    snapshot.drawCommands.assign(commands.commands().begin(), commands.commands().end());
    pipelineCounters.drawCommandsBuilt += snapshot.drawCommands.size();
    snapshot.counters = pipelineCounters.toDebugString();

    auto presentableTiles = presentableDisplayTiles(snapshot.displayTiles);
    if (!committedVisualFrame
        || committedVisualFrame->semantics != request.formula.semanticsHash)
    {
        committedVisualFrame = CommittedVisualFrame{
            .semantics = request.formula.semanticsHash,
            .viewport = request,
            .tiles = std::move(presentableTiles)
        };
    }
    else if (!presentableTiles.empty())
    {
        committedVisualFrame = CommittedVisualFrame{
            .semantics = request.formula.semanticsHash,
            .viewport = request,
            .tiles = std::move(presentableTiles)
        };
    }

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
        .devicePixelRatio = appState.devicePixelRatio
    };
}

FrameCommandBuffer FramePipeline::buildCommands(std::vector<DisplayTile> &displayTiles,
                                                const ViewportRequest &request,
                                                const UploadPlan &uploadPlan)
{
    resources.setPlotViewport(request.xRange, request.yRange);
    resources.setGridState(request.xRange, request.yRange, request.framebufferWidth, request.framebufferHeight);

    std::vector<RenderTileInstance> instances;
    instances.reserve(displayTiles.size());
    std::vector<RenderTileInstance> debugInstances;
    debugInstances.reserve(appState.debug ? displayTiles.size() : 0);
    for (auto &tile : displayTiles)
    {
        auto regionSlice = tile.gpuSlice;
        if (tile.cpuRegion)
        {
            if (regionSlice.textureId == 0
                && containsTileKey(uploadPlan.textureUploads, tile.sourceKey))
            {
                if (const auto payloadIt = regionPayloads.find(tile.cpuRegion->id);
                    payloadIt != regionPayloads.end())
                {
                    regionSlice = resources.registerRegionImage(*tile.cpuRegion, payloadIt->second.pixels);
                    tile.gpuSlice = regionSlice;
                }
            }
        }

        if (auto instance = normalPlotInstanceForDisplayTile(tile, regionSlice))
        {
            instances.push_back(*instance);
        }
        if (appState.debug)
        {
            debugInstances.push_back({
                .key = tile.sourceKey,
                .worldBounds = tile.worldBounds,
                .visualState = tile.visualState == TileVisualState::Missing
                    ? TileVisualState::DebugMissing
                    : (tile.visualState == TileVisualState::MixedRegion
                        ? TileVisualState::DebugMixed
                        : TileVisualState::DebugUniform),
                .uvRect = tile.uvRect
            });
        }
    }

    const auto plotInstanceCount = instances.size();
    debugStats.plotTiles = plotInstanceCount;
    resources.setPlotInstances(std::move(instances));
    const auto debugPlotInstanceCount = debugInstances.size();
    resources.setDebugPlotInstances(std::move(debugInstances));
    resources.setOverlayRects(buildOverlayRects());
    resources.setOverlayTextRuns(buildOverlayTextRuns());

    FrameCommandBuffer buffer;
    buffer.add({
        .layer = RenderLayer::Grid,
        .pipeline = resources.gridPipeline(),
        .geometry = resources.staticQuadGeometry(),
        .material = resources.gridMaterial(),
        .sortKey = 10
    });
    if (plotInstanceCount > 0)
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
    if (resources.overlayTextRunCount() > 0)
    {
        buffer.add({
            .layer = RenderLayer::Text,
            .pipeline = resources.textPipeline(),
            .geometry = resources.staticQuadGeometry(),
            .material = resources.textMaterial(),
            .instanceRange = BufferRange{4, 0, static_cast<uint32_t>(resources.overlayTextRunCount())},
            .sortKey = 1
        });
    }
    buffer.freezeAndSort();
    return buffer;
}

std::vector<OverlayRect> FramePipeline::buildOverlayRects() const
{
    std::vector<OverlayRect> rects;
    const std::array white{0.92f, 0.96f, 1.0f, 1.0f};
    const std::array panel{0.06f, 0.07f, 0.09f, 0.78f};
    const std::array dark{0.06f, 0.07f, 0.09f, 0.88f};
    const std::array border{0.70f, 0.76f, 0.84f, 0.85f};
    const std::array button{0.16f, 0.18f, 0.22f, 0.86f};
    const std::array buttonPrimary{0.02f, 0.36f, 0.72f, 0.90f};
    const std::array buttonActive{0.95f, 0.68f, 0.18f, 0.90f};
    const std::array inputFill{0.03f, 0.04f, 0.06f, 0.72f};
    const auto scale = uiScaleFor(appState);

    const auto pushPixelRect = [&](const UiPixelRect &rect, const std::array<float, 4> &color)
    {
        const auto ndc = toNdcRect(rect, appState.framebufferWidth, appState.framebufferHeight);
        if (ndc.xMin < ndc.xMax && ndc.yMin < ndc.yMax)
        {
            rects.push_back({ndc.xMin, ndc.xMax, ndc.yMin, ndc.yMax, color});
        }
    };
    const auto pushPixelBorder = [&](const UiPixelRect &rect,
                                     const float thicknessPx,
                                     const std::array<float, 4> &color)
    {
        if (rect.width() <= 0.0f || rect.height() <= 0.0f)
        {
            return;
        }
        const auto thickness = std::min({thicknessPx, rect.width() * 0.5f, rect.height() * 0.5f});
        pushPixelRect(UiPixelRect{rect.left, rect.top, rect.right, rect.top + thickness}, color);
        pushPixelRect(UiPixelRect{rect.left, rect.bottom - thickness, rect.right, rect.bottom}, color);
        pushPixelRect(UiPixelRect{rect.left, rect.top, rect.left + thickness, rect.bottom}, color);
        pushPixelRect(UiPixelRect{rect.right - thickness, rect.top, rect.right, rect.bottom}, color);
    };
    const auto pushButtons = [&](const std::vector<UiButtonLayout> &buttons)
    {
        for (const auto &uiButton : buttons)
        {
            const auto color = uiButton.active ? buttonActive : (uiButton.primary ? buttonPrimary : button);
            pushPixelRect(uiButton.bounds, color);
            pushPixelBorder(uiButton.bounds, 1.0f * scale, border);
        }
    };

    if (appState.formulaInput.active)
    {
        const auto layout = formulaOverlayLayoutFor(appState);
        pushPixelRect(layout.panel, dark);
        pushPixelBorder(layout.panel, 2.0f * scale, border);
        pushPixelRect(layout.input, inputFill);
        pushPixelBorder(layout.input, 1.0f * scale, border);
        pushButtons(layout.buttons);

        const auto textBounds = UiPixelRect{
            layout.input.left + 6.0f * scale,
            layout.input.top,
            std::max(layout.input.left + 7.0f * scale, layout.input.right - 6.0f * scale),
            layout.input.bottom
        };
        const auto textNdc = toNdcRect(textBounds, appState.framebufferWidth, appState.framebufferHeight);
        const auto inputNdc = toNdcRect(layout.input, appState.framebufferWidth, appState.framebufferHeight);
        const auto visible = visibleFormulaText(appState.formulaInput.buffer,
                                                appState.formulaInput.cursor,
                                                textNdc.xMin,
                                                textNdc.xMax,
                                                layout.fontPx,
                                                appState.framebufferWidth);
        const auto cursorX = std::min(
            textNdc.xMax,
            textNdc.xMin + textAdvanceNdc(visible.cursor, layout.fontPx, appState.framebufferWidth));
        const auto cursorWidth = appState.framebufferWidth > 0
            ? std::max(0.004f, 2.0f * scale / static_cast<float>(appState.framebufferWidth) * 2.0f)
            : 0.006f;
        rects.push_back({
            cursorX,
            std::min(cursorX + cursorWidth, inputNdc.xMax),
            inputNdc.yMin + textHeightNdc(3.0f * scale, appState.framebufferHeight),
            inputNdc.yMax - textHeightNdc(3.0f * scale, appState.framebufferHeight),
            white
        });
    }
    else
    {
        const auto layout = statusOverlayLayoutFor(appState);
        pushPixelRect(layout.panel, panel);
        pushPixelBorder(layout.panel, 1.0f * scale, border);
        pushButtons(layout.buttons);
    }

    if (appState.debug)
    {
        const auto layout = debugOverlayLayoutFor(
            debugStats,
            pipelineCounters,
            appState.framebufferWidth,
            appState.framebufferHeight,
            scale);
        const auto borderHeight = gx::textHeightNdc(1.0f * scale, appState.framebufferHeight);
        rects.push_back({layout.panelLeft, layout.panelRight, layout.panelBottom, layout.panelTop, dark});
        rects.push_back({layout.panelLeft, layout.panelRight, layout.panelTop - borderHeight, layout.panelTop, border});
    }

    return rects;
}

std::vector<OverlayTextRun> FramePipeline::buildOverlayTextRuns() const
{
    std::vector<OverlayTextRun> runs;
    const std::array white{0.92f, 0.96f, 1.0f, 1.0f};
    const std::array muted{0.78f, 0.84f, 0.92f, 0.9f};
    const std::array label{0.80f, 0.84f, 0.90f, 0.74f};
    const std::array error{1.0f, 0.48f, 0.42f, 0.96f};
    const auto scale = uiScaleFor(appState);
    const auto logicalWidth = static_cast<float>(std::max(1, appState.framebufferWidth)) / scale;
    const auto logicalHeight = static_cast<float>(std::max(1, appState.framebufferHeight)) / scale;

    const auto pushPixelText = [&](std::string text,
                                   const float x,
                                   const float y,
                                   const float pixelHeight,
                                   const std::array<float, 4> &color)
    {
        if (text.empty())
        {
            return;
        }
        runs.push_back({
            .text = std::move(text),
            .x = normalizedPixelX(x, appState.framebufferWidth),
            .y = normalizedPixelYFromTop(y, appState.framebufferHeight),
            .pixelHeight = pixelHeight,
            .color = color
        });
    };
    const auto pushButtonText = [&](const UiButtonLayout &button, const float pixelHeight)
    {
        const auto textWidth = textAdvancePx(button.text.size(), pixelHeight);
        const auto lineBox = pixelHeight * 1.28f;
        const auto x = button.bounds.left + std::max(2.0f * scale, (button.bounds.width() - textWidth) * 0.5f);
        const auto y = button.bounds.top + std::max(2.0f * scale, (button.bounds.height() - lineBox) * 0.5f);
        pushPixelText(button.text, x, y, pixelHeight, white);
    };

    const auto xAxisY = std::clamp(toNdc(0.0, appState.yRange), -0.93f, 0.93f);
    const auto yAxisX = std::clamp(toNdc(0.0, appState.xRange), -0.93f, 0.93f);
    const auto xStep = pickNiceStep(
        appState.xRange.size(),
        desiredTickCountForAxis(appState.framebufferWidth, static_cast<int>(112.0f * scale)));
    const auto yStep = pickNiceStep(
        appState.yRange.size(),
        desiredTickCountForAxis(appState.framebufferHeight, static_cast<int>(84.0f * scale)));
    const auto axisLabelPx = ((logicalWidth < 420.0f || logicalHeight < 340.0f) ? 11.0f : 13.0f) * scale;
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
        auto text = formatTick(x);
        runs.push_back({
            .text = text,
            .x = clampTextX(toNdc(x, appState.xRange) - 0.035f, text, axisLabelPx, appState.framebufferWidth),
            .y = clampTextY(xAxisY - 0.035f, axisLabelPx, appState.framebufferHeight),
            .pixelHeight = axisLabelPx,
            .color = label
        });
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
        auto text = formatTick(y);
        runs.push_back({
            .text = text,
            .x = clampTextX(yAxisX + 0.012f, text, axisLabelPx, appState.framebufferWidth),
            .y = clampTextY(toNdc(y, appState.yRange) + 0.020f, axisLabelPx, appState.framebufferHeight),
            .pixelHeight = axisLabelPx,
            .color = label
        });
    }

    if (appState.formulaInput.active)
    {
        const auto layout = formulaOverlayLayoutFor(appState);
        const auto textBounds = UiPixelRect{
            layout.input.left + 6.0f * scale,
            layout.input.top,
            std::max(layout.input.left + 7.0f * scale, layout.input.right - 6.0f * scale),
            layout.input.bottom
        };
        const auto textNdc = toNdcRect(textBounds, appState.framebufferWidth, appState.framebufferHeight);
        const auto visible = visibleFormulaText(appState.formulaInput.buffer,
                                                appState.formulaInput.cursor,
                                                textNdc.xMin,
                                                textNdc.xMax,
                                                layout.fontPx,
                                                appState.framebufferWidth);
        if (!layout.labelText.empty())
        {
            pushPixelText(layout.labelText, layout.labelX, layout.textY, layout.fontPx, muted);
        }
        pushPixelText(visible.text, textBounds.left, layout.textY, layout.fontPx, white);
        if (!appState.formulaInput.error.empty())
        {
            const auto messageLeftNdc = normalizedPixelX(layout.input.left, appState.framebufferWidth);
            const auto messageRightNdc = normalizedPixelX(layout.panel.right - 8.0f * scale, appState.framebufferWidth);
            const auto visibleError = visibleFormulaText(appState.formulaInput.error,
                                                         0,
                                                         messageLeftNdc,
                                                         messageRightNdc,
                                                         layout.messageFontPx,
                                                         appState.framebufferWidth);
            pushPixelText(visibleError.text, layout.input.left, layout.messageY, layout.messageFontPx, error);
        }
        for (const auto &button : layout.buttons)
        {
            pushButtonText(button, std::min(layout.fontPx, 14.0f * scale));
        }
    }
    else
    {
        const auto layout = statusOverlayLayoutFor(appState);
        if (!layout.formulaLabel.empty())
        {
            pushPixelText(layout.formulaLabel, layout.formulaLabelX, layout.textY, layout.fontPx, muted);
        }
        const auto formulaNdc = toNdcRect(layout.formula, appState.framebufferWidth, appState.framebufferHeight);
        const auto visible = visibleFormulaText(appState.formulaExpression,
                                                appState.formulaExpression.size(),
                                                formulaNdc.xMin,
                                                formulaNdc.xMax,
                                                layout.fontPx,
                                                appState.framebufferWidth);
        pushPixelText(visible.text, layout.formulaTextX, layout.textY, layout.fontPx, white);
        for (const auto &button : layout.buttons)
        {
            pushButtonText(button, layout.fontPx);
        }
    }

    if (appState.debug)
    {
        const auto lines = debugOverlayLines(debugStats, pipelineCounters);
        const auto layout = debugOverlayLayoutFor(
            debugStats,
            pipelineCounters,
            appState.framebufferWidth,
            appState.framebufferHeight,
            scale);
        for (size_t index = 0; index < lines.size(); ++index)
        {
            runs.push_back({
                .text = lines[index],
                .x = layout.textX,
                .y = layout.textY[index],
                .pixelHeight = layout.pixelHeights[index],
                .color = index == 0 ? white : muted
            });
        }
    }

    return runs;
}
}
