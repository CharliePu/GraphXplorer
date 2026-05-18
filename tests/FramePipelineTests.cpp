#include "catch.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <utility>

#include "../src/Compute/ComputeBackend.h"
#include "../src/App/FramePipeline.h"
#include "../src/Tile/TileMath.h"

namespace
{
using namespace std::chrono_literals;

bool insideNdc(const float value)
{
    return value >= -1.0f && value <= 1.0f;
}

void requireValidOverlayGeometry(gx::FramePipeline &pipeline)
{
    for (const auto &rect : pipeline.renderResources().overlayRectData())
    {
        CHECK(rect.xMin < rect.xMax);
        CHECK(rect.yMin < rect.yMax);
        CHECK(insideNdc(rect.xMin));
        CHECK(insideNdc(rect.xMax));
        CHECK(insideNdc(rect.yMin));
        CHECK(insideNdc(rect.yMax));
    }

    for (const auto &run : pipeline.renderResources().overlayTextRunData())
    {
        CHECK(insideNdc(run.x));
        CHECK(insideNdc(run.y));
        CHECK(run.pixelHeight > 0.0f);
        CHECK(run.color[3] > 0.0f);
    }
}

size_t normalDisplayInstanceCount(const gx::FrameSnapshot &snapshot)
{
    return static_cast<size_t>(std::ranges::count_if(snapshot.displayTiles, [](const gx::DisplayTile &tile)
    {
        return tile.visualState != gx::TileVisualState::UniformFalse;
    }));
}

size_t unresolvedMixedTileCount(const gx::FramePipeline &pipeline, const gx::FrameSnapshot &snapshot)
{
    if (!snapshot.formula)
    {
        return 0;
    }

    return static_cast<size_t>(std::ranges::count_if(snapshot.visibleCover, [&](const gx::TileKey &key)
    {
        const auto *record = pipeline.tiles().find(key, snapshot.formula->semanticsHash);
        return record
            && record->valueState == gx::TileValueState::Mixed
            && !record->regionPixels.has_value();
    }));
}

struct PumpResult
{
    gx::FrameSnapshot snapshot{};
    size_t transactionCount{0};
};

template<typename Predicate>
PumpResult pumpUntil(gx::FramePipeline &pipeline, Predicate predicate, const int maxFrames = 240)
{
    PumpResult result;
    for (auto frame = 0; frame < maxFrames; ++frame)
    {
        result.snapshot = pipeline.process(gx::RenderTickEvent{});
        result.transactionCount += result.snapshot.appliedTransactions.size();
        if (predicate(result.snapshot))
        {
            return result;
        }
        std::this_thread::sleep_for(1ms);
    }
    return result;
}

class BlockingBackend final : public gx::ComputeBackend
{
public:
    explicit BlockingBackend(std::atomic<bool> &release): release{release}
    {
    }

    [[nodiscard]] gx::BackendCapabilities capabilities() const override
    {
        return {};
    }

    gx::BatchResult classifyIntervals(const gx::IntervalBatchView &batch,
                                      std::span<gx::TileClassificationResult> out) override
    {
        ++classifyCalls;
        recordMax(maxClassifyBatch, batch.keys.size());
        waitUntilReleased(batch.cancelled);
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = {batch.keys[i], gx::TileClassification::UniformFalse, Interval{0.0, 0.0}};
        }
        return {true, out.size(), {}};
    }

    gx::BatchResult rasterizeRegions(const gx::RasterBatchView &batch,
                                     std::span<gx::RegionOutput> out) override
    {
        ++rasterCalls;
        recordMax(maxRasterBatch, batch.keys.size());
        waitUntilReleased(batch.cancelled);
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i].key = batch.keys[i];
            out[i].width = batch.pixelsPerAxis;
            out[i].height = batch.pixelsPerAxis;
            out[i].pixels.assign(static_cast<size_t>(batch.pixelsPerAxis) * batch.pixelsPerAxis, 0);
        }
        return {true, out.size(), {}};
    }

    std::atomic<int> classifyCalls{0};
    std::atomic<int> rasterCalls{0};
    std::atomic<size_t> maxClassifyBatch{0};
    std::atomic<size_t> maxRasterBatch{0};

private:
    static void recordMax(std::atomic<size_t> &target, const size_t value)
    {
        auto current = target.load(std::memory_order_relaxed);
        while (current < value
            && !target.compare_exchange_weak(current, value, std::memory_order_relaxed))
        {
        }
    }

    void waitUntilReleased(const std::function<bool()> &cancelled) const
    {
        while (!release.load(std::memory_order_acquire))
        {
            if (cancelled && cancelled())
            {
                return;
            }
            std::this_thread::sleep_for(1ms);
        }
    }

    std::atomic<bool> &release;
};

class FixedFrameBudgetPolicy final : public gx::FrameBudgetPolicy
{
public:
    explicit FixedFrameBudgetPolicy(gx::FrameWorkBudget nextBudget): budget{nextBudget}
    {
    }

    [[nodiscard]] gx::FrameWorkBudget beginFrame(const gx::FrameBudgetContext &context) override
    {
        lastContext = context;
        ++beginCalls;
        return budget;
    }

    void endFrame(const gx::FrameBudgetFeedback &feedback) override
    {
        lastFeedback = feedback;
        ++endCalls;
    }

    gx::FrameWorkBudget budget{};
    gx::FrameBudgetContext lastContext{};
    gx::FrameBudgetFeedback lastFeedback{};
    int beginCalls{0};
    int endCalls{0};
};
}

TEST_CASE("FramePipeline delegates tunable budgets to an injected policy",
          "[FramePipeline][Responsiveness]")
{
    std::atomic<bool> releaseBackend{true};
    auto backend = std::make_unique<BlockingBackend>(releaseBackend);
    auto policy = std::make_unique<FixedFrameBudgetPolicy>(gx::FrameWorkBudget{
        .completedTileApplyBudget = 500us,
        .tilePlan = {
            .maxIntervalJobsPerFrame = 0,
            .maxRasterJobsPerFrame = 0
        },
        .upload = {
            .maxTextureBytesPerFrame = 0,
            .maxBufferBytesPerFrame = 0,
            .maxTextureSlicesPerFrame = 0,
            .maxTileInstanceUpdatesPerFrame = 0
        },
        .renderUpload = {
            .maxTextureBytesPerFrame = 1234,
            .maxBufferBytesPerFrame = 5678,
            .maxTextureSlicesPerFrame = 3,
            .maxTileInstanceUpdatesPerFrame = 9
        },
        .maxSeedCells = 1,
        .refinementDepth = 1,
        .submitTileJobs = false,
        .allowGpuRaster = false
    });
    auto *policyView = policy.get();
    gx::FramePipeline pipeline{std::move(backend), gx::FramePipelineOptions{}, std::move(policy)};

    [[maybe_unused]] const auto snapshot = pipeline.process(gx::ViewportChangedEvent{
        .xRange = Interval{-2.0, 2.0},
        .yRange = Interval{-2.0, 2.0},
        .framebufferWidth = 512,
        .framebufferHeight = 512,
        .devicePixelRatio = 2.0
    });

    CHECK(policyView->beginCalls == 1);
    CHECK(policyView->endCalls == 1);
    REQUIRE(policyView->lastContext.framebuffer.has_value());
    CHECK(policyView->lastContext.framebuffer->framebufferWidth == 512);
    CHECK(policyView->lastContext.framebuffer->framebufferHeight == 512);
    CHECK(policyView->lastContext.framebuffer->devicePixelRatio == 2.0);
    CHECK(policyView->lastContext.pendingCompletions == 0);
    CHECK(policyView->lastContext.inFlightJobs == 0);
    CHECK(policyView->lastFeedback.submittedJobs == 0);
    CHECK(pipeline.renderUploadBudget().maxTextureBytesPerFrame == 1234);
    CHECK(pipeline.renderUploadBudget().maxBufferBytesPerFrame == 5678);
    CHECK(pipeline.renderUploadBudget().maxTextureSlicesPerFrame == 3);
    CHECK(pipeline.renderUploadBudget().maxTileInstanceUpdatesPerFrame == 9);
}

TEST_CASE("FramePipeline drives contract-first flow from input event to frame snapshot", "[FramePipeline]")
{
    gx::FramePipeline pipeline;

    const auto viewportSnapshot = pipeline.process(gx::ViewportChangedEvent{
        .xRange = Interval{-2.0, 2.0},
        .yRange = Interval{-2.0, 2.0},
        .framebufferWidth = 512,
        .framebufferHeight = 512
    });

    REQUIRE(viewportSnapshot.viewportRequest.has_value());
    REQUIRE(viewportSnapshot.formula.has_value());
    CHECK(viewportSnapshot.viewportRequest->valid());

    const auto result = pumpUntil(pipeline, [](const gx::FrameSnapshot &snapshot)
    {
        return !snapshot.visibleCover.empty() && !snapshot.appliedTransactions.empty();
    });
    const auto &snapshot = result.snapshot;

    CHECK(result.transactionCount > 0);
    CHECK_FALSE(snapshot.visibleCover.empty());
    CHECK_FALSE(snapshot.drawCommands.empty());
    CHECK(pipeline.renderResources().plotInstanceCount() == normalDisplayInstanceCount(snapshot));
    CHECK(std::ranges::any_of(snapshot.drawCommands, [](const gx::DrawCommand &command) {
        return command.layer == gx::RenderLayer::Grid;
    }));
    CHECK(snapshot.counters.find("tileDeltasApplied=") != std::string::npos);
}

TEST_CASE("FramePipeline renders stable placeholders for unresolved mixed tiles", "[FramePipeline]")
{
    gx::FramePipeline pipeline;

    [[maybe_unused]] const auto viewportSnapshot = pipeline.process(gx::ViewportChangedEvent{
        .xRange = Interval{-20.0, 20.0},
        .yRange = Interval{-20.0, 20.0},
        .framebufferWidth = 800,
        .framebufferHeight = 800
    });

    const auto result = pumpUntil(pipeline, [](const gx::FrameSnapshot &snapshot)
    {
        return !snapshot.visibleCover.empty() && !snapshot.appliedTransactions.empty();
    });
    const auto &snapshot = result.snapshot;

    REQUIRE(snapshot.formula.has_value());
    REQUIRE_FALSE(snapshot.visibleCover.empty());
    const auto unresolved = unresolvedMixedTileCount(pipeline, snapshot);
    CHECK(pipeline.renderResources().plotInstanceCount() == normalDisplayInstanceCount(snapshot));
    if (unresolved > 0)
    {
        CHECK(std::ranges::any_of(snapshot.displayTiles, [](const gx::DisplayTile &tile)
        {
            return tile.visualState == gx::TileVisualState::Missing;
        }));
    }
    CHECK_FALSE(std::ranges::any_of(snapshot.drawCommands, [](const gx::DrawCommand &command) {
        return command.layer == gx::RenderLayer::Contour;
    }));
}

TEST_CASE("FramePipeline compiles only on formula changes", "[FramePipeline]")
{
    gx::FramePipeline pipeline;
    const auto initialCompiles = pipeline.counters().formulasCompiled;

    const auto viewportSnapshot = pipeline.process(gx::ViewportChangedEvent{
        .xRange = Interval{-3.0, 3.0},
        .yRange = Interval{-3.0, 3.0},
        .framebufferWidth = 512,
        .framebufferHeight = 512
    });
    (void)viewportSnapshot;
    CHECK(pipeline.counters().formulasCompiled == initialCompiles);

    const auto formulaSnapshot = pipeline.process(gx::FormulaInputEvent{.expression = "x+y>0"});
    (void)formulaSnapshot;
    CHECK(pipeline.counters().formulasCompiled == initialCompiles + 1);
}

TEST_CASE("FramePipeline settles identity formulas as coarse uniform authorities", "[FramePipeline]")
{
    gx::FramePipeline pipeline;

    [[maybe_unused]] const auto inputSnapshot = pipeline.process(gx::FormulaInputEvent{.expression = "x=x"});
    const auto expectedLevel = gx::seedTileLevelForViewport(*inputSnapshot.viewportRequest);
    const auto result = pumpUntil(pipeline, [](const gx::FrameSnapshot &frame)
    {
        if (!frame.viewportRequest)
        {
            return false;
        }
        const auto expectedLevel = gx::seedTileLevelForViewport(*frame.viewportRequest);
        return !frame.displayTiles.empty()
            && std::ranges::all_of(frame.displayTiles, [](const gx::DisplayTile &tile)
            {
                return tile.desiredKey == tile.sourceKey
                    && tile.visualState == gx::TileVisualState::UniformTrue
                    && !tile.isFallback
                    && !tile.clippedFallback;
            })
            && std::ranges::all_of(frame.displayTiles, [expectedLevel](const gx::DisplayTile &tile)
            {
                return tile.sourceKey.level == expectedLevel;
            });
    });
    const auto &snapshot = result.snapshot;

    REQUIRE(snapshot.formula.has_value());
    REQUIRE_FALSE(snapshot.displayTiles.empty());
    CHECK(snapshot.displayTiles.size() == 4);
    CHECK(std::ranges::all_of(snapshot.displayTiles, [expectedLevel](const gx::DisplayTile &tile)
    {
        return tile.sourceKey.level == expectedLevel
            && tile.desiredKey == tile.sourceKey
            && tile.visualState == gx::TileVisualState::UniformTrue
            && !tile.isFallback
            && !tile.clippedFallback;
    }));
    CHECK(pipeline.renderResources().plotInstanceCount() == snapshot.displayTiles.size());
}

TEST_CASE("FramePipeline exposes formula input overlay as command data", "[FramePipeline]")
{
    gx::FramePipeline pipeline;

    auto snapshot = pipeline.process(gx::BeginFormulaInputEvent{});
    CHECK(pipeline.state().formulaInput.active);
    CHECK_FALSE(snapshot.drawCommands.empty());
    CHECK(pipeline.renderResources().overlayTextRunCount() >= 2);
    CHECK(std::ranges::any_of(snapshot.drawCommands, [&pipeline](const gx::DrawCommand &command) {
        return command.layer == gx::RenderLayer::Text
            && command.pipeline == pipeline.renderResources().textPipeline();
    }));

    const auto appendSnapshot = pipeline.process(gx::AppendFormulaInputEvent{" + x"});
    (void)appendSnapshot;
    snapshot = pipeline.process(gx::SubmitFormulaInputEvent{});

    CHECK_FALSE(pipeline.state().formulaInput.active);
    CHECK(pipeline.state().formulaExpression.find("+ x") != std::string::npos);
    CHECK(snapshot.counters.find("formulasCompiled=") != std::string::npos);
}

TEST_CASE("FramePipeline keeps invalid formula submissions in the editor", "[FramePipeline]")
{
    gx::FramePipeline pipeline;

    [[maybe_unused]] auto snapshot = pipeline.process(gx::BeginFormulaInputEvent{});
    snapshot = pipeline.process(gx::AppendFormulaInputEvent{"+"});
    snapshot = pipeline.process(gx::SubmitFormulaInputEvent{});

    CHECK(pipeline.state().formulaInput.active);
    CHECK_FALSE(pipeline.state().formulaInput.error.empty());
    CHECK(pipeline.state().formulaExpression == "x<=y");
    CHECK(pipeline.renderResources().overlayTextRunCount() >= 3);
    requireValidOverlayGeometry(pipeline);

    snapshot = pipeline.process(gx::BackspaceFormulaInputEvent{});
    CHECK(pipeline.state().formulaInput.error.empty());
}

TEST_CASE("FramePipeline exposes idle chrome for current formula and actions", "[FramePipeline]")
{
    gx::FramePipeline pipeline;

    const auto snapshot = pipeline.process(gx::ViewportChangedEvent{
        .xRange = Interval{-20.0, 20.0},
        .yRange = Interval{-20.0, 20.0},
        .framebufferWidth = 800,
        .framebufferHeight = 600
    });

    CHECK_FALSE(pipeline.state().formulaInput.active);
    CHECK(pipeline.renderResources().overlayRectCount() >= 8);
    CHECK(pipeline.renderResources().overlayTextRunCount() >= 4);
    CHECK(std::ranges::any_of(snapshot.drawCommands, [&pipeline](const gx::DrawCommand &command) {
        return command.layer == gx::RenderLayer::Text
            && command.pipeline == pipeline.renderResources().textPipeline();
    }));
    requireValidOverlayGeometry(pipeline);
}

TEST_CASE("FramePipeline keeps UI overlay geometry valid across aspect ratios", "[FramePipeline]")
{
    const std::array sizes{
        std::pair{240, 320},
        std::pair{360, 640},
        std::pair{480, 900},
        std::pair{800, 800},
        std::pair{1280, 720},
        std::pair{1600, 500}
    };

    for (const auto [width, height] : sizes)
    {
        gx::FramePipeline pipeline;
        [[maybe_unused]] auto snapshot = pipeline.process(gx::ViewportChangedEvent{
            .xRange = Interval{-20.0, 20.0},
            .yRange = Interval{-20.0, 20.0},
            .framebufferWidth = width,
            .framebufferHeight = height
        });
        snapshot = pipeline.process(gx::DebugToggleEvent{true});
        snapshot = pipeline.process(gx::BeginFormulaInputEvent{});
        snapshot = pipeline.process(gx::AppendFormulaInputEvent{
            " + sin(x) * cos(y) + sqrt(abs(x-y)) + x^4 - y^4"
        });

        requireValidOverlayGeometry(pipeline);
        CHECK(pipeline.renderResources().overlayRectCount() >= 8);
        CHECK(pipeline.renderResources().overlayTextRunCount() >= 6);
    }
}

TEST_CASE("FramePipeline debug mode adds chunk-frame instances through command renderer", "[FramePipeline]")
{
    gx::FramePipeline pipeline;

    [[maybe_unused]] auto snapshot = pipeline.process(gx::ViewportChangedEvent{
        .xRange = Interval{-2.0, 2.0},
        .yRange = Interval{-2.0, 2.0},
        .framebufferWidth = 512,
        .framebufferHeight = 512
    });
    snapshot = pumpUntil(pipeline, [](const gx::FrameSnapshot &frame)
    {
        return !frame.visibleCover.empty() && !frame.appliedTransactions.empty();
    }).snapshot;
    REQUIRE_FALSE(snapshot.visibleCover.empty());
    const auto normalInstanceCount = pipeline.renderResources().plotInstanceCount();
    REQUIRE(normalInstanceCount == normalDisplayInstanceCount(snapshot));

    snapshot = pipeline.process(gx::DebugToggleEvent{true});
    CHECK(pipeline.state().debug);
    const auto debugInstanceCount = pipeline.renderResources().debugPlotInstanceCount();
    CHECK(pipeline.renderResources().plotInstanceCount() == normalDisplayInstanceCount(snapshot));
    CHECK(pipeline.renderResources().plotInstanceCount() >= normalInstanceCount);
    CHECK(debugInstanceCount == snapshot.displayTiles.size());
    CHECK(std::ranges::any_of(snapshot.drawCommands, [debugInstanceCount](const gx::DrawCommand &command) {
        return command.layer == gx::RenderLayer::Contour
            && command.instanceRange.count == static_cast<uint32_t>(debugInstanceCount);
    }));
    const auto overlayText = pipeline.renderResources().overlayTextRunData();
    CHECK(std::ranges::none_of(overlayText, [](const gx::OverlayTextRun &run) {
        return run.text == "debug frames";
    }));
    CHECK(std::ranges::any_of(overlayText, [](const gx::OverlayTextRun &run) {
        return run.text.find("tiles:") == 0;
    }));
    CHECK(std::ranges::any_of(overlayText, [](const gx::OverlayTextRun &run) {
        return run.text.find("processing:") == 0;
    }));
}

TEST_CASE("FramePipeline viewport events return without waiting for compute backend", "[FramePipeline][Responsiveness]")
{
    std::atomic<bool> releaseBackend{false};
    auto backend = std::make_unique<BlockingBackend>(releaseBackend);
    auto *backendView = backend.get();
    gx::FramePipeline pipeline{std::move(backend)};

    const auto start = std::chrono::steady_clock::now();
    [[maybe_unused]] const auto snapshot = pipeline.process(gx::ViewportChangedEvent{
        .xRange = Interval{-2.0, 2.0},
        .yRange = Interval{-2.0, 2.0},
        .framebufferWidth = 512,
        .framebufferHeight = 512
    });
    const auto elapsed = std::chrono::steady_clock::now() - start;

    releaseBackend.store(true, std::memory_order_release);
    CHECK(elapsed < 50ms);
    CHECK(backendView->rasterCalls.load() == 0);
}

TEST_CASE("FramePipeline keeps same-formula tile work across viewport requests", "[FramePipeline][Responsiveness]")
{
    std::atomic<bool> releaseBackend{false};
    auto backend = std::make_unique<BlockingBackend>(releaseBackend);
    auto *backendView = backend.get();
    gx::FramePipeline pipeline{std::move(backend)};

    const auto first = pipeline.process(gx::ViewportChangedEvent{
        .xRange = Interval{-2.0, 2.0},
        .yRange = Interval{-2.0, 2.0},
        .framebufferWidth = 512,
        .framebufferHeight = 512
    });
    REQUIRE(first.viewportRequest.has_value());

    for (auto spin = 0; spin < 100 && backendView->classifyCalls.load() == 0; ++spin)
    {
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(backendView->classifyCalls.load() > 0);

    const auto second = pipeline.process(gx::ViewportChangedEvent{
        .xRange = Interval{-1.0, 1.0},
        .yRange = Interval{-1.0, 1.0},
        .framebufferWidth = 512,
        .framebufferHeight = 512
    });
    REQUIRE(second.viewportRequest.has_value());
    REQUIRE(second.viewportRequest->header.requestId != first.viewportRequest->header.requestId);

    releaseBackend.store(true, std::memory_order_release);
    const auto result = pumpUntil(pipeline, [](const gx::FrameSnapshot &snapshot)
    {
        return !snapshot.appliedTransactions.empty();
    });

    REQUIRE_FALSE(result.snapshot.appliedTransactions.empty());
    CHECK(std::ranges::any_of(result.snapshot.appliedTransactions, [&](const gx::TileTransaction &tx)
    {
        return tx.header.requestId == first.viewportRequest->header.requestId
            && tx.semanticsHash == first.viewportRequest->formula.semanticsHash;
    }));
}

TEST_CASE("FramePipeline submits visible tile work in backend batches", "[FramePipeline][Responsiveness]")
{
    std::atomic<bool> releaseBackend{true};
    auto backend = std::make_unique<BlockingBackend>(releaseBackend);
    auto *backendView = backend.get();
    gx::FramePipeline pipeline{std::move(backend)};

    [[maybe_unused]] const auto snapshot = pipeline.process(gx::ViewportChangedEvent{
        .xRange = Interval{-2.0, 2.0},
        .yRange = Interval{-2.0, 2.0},
        .framebufferWidth = 512,
        .framebufferHeight = 512
    });

    for (auto spin = 0; spin < 100 && backendView->classifyCalls.load() == 0; ++spin)
    {
        std::this_thread::sleep_for(1ms);
    }

    REQUIRE(backendView->classifyCalls.load() > 0);
    CHECK(backendView->maxClassifyBatch.load() > 1);
}

TEST_CASE("FramePipeline forwards worker completions to a frame wake callback", "[FramePipeline][Responsiveness]")
{
    std::atomic<bool> releaseBackend{true};
    auto backend = std::make_unique<BlockingBackend>(releaseBackend);
    gx::FramePipeline pipeline{std::move(backend)};

    std::mutex mutex;
    std::condition_variable completed;
    auto wakeCount = 0;
    pipeline.setFrameWakeCallback([&]
    {
        {
            std::lock_guard lock(mutex);
            ++wakeCount;
        }
        completed.notify_one();
    });

    [[maybe_unused]] const auto snapshot = pipeline.process(gx::ViewportChangedEvent{
        .xRange = Interval{-2.0, 2.0},
        .yRange = Interval{-2.0, 2.0},
        .framebufferWidth = 512,
        .framebufferHeight = 512
    });

    std::unique_lock lock(mutex);
    REQUIRE(completed.wait_for(lock, 1s, [&] { return wakeCount > 0; }));
    lock.unlock();
    CHECK(pipeline.pendingCompletionCount() > 0);
}

TEST_CASE("FramePipeline render ticks do not synchronously wait for compute backend", "[FramePipeline][Responsiveness]")
{
    std::atomic<bool> releaseBackend{false};
    auto backend = std::make_unique<BlockingBackend>(releaseBackend);
    gx::FramePipeline pipeline{std::move(backend)};

    [[maybe_unused]] const auto viewportSnapshot = pipeline.process(gx::ViewportChangedEvent{
        .xRange = Interval{-2.0, 2.0},
        .yRange = Interval{-2.0, 2.0},
        .framebufferWidth = 512,
        .framebufferHeight = 512
    });

    const auto start = std::chrono::steady_clock::now();
    [[maybe_unused]] const auto tickSnapshot = pipeline.process(gx::RenderTickEvent{});
    const auto elapsed = std::chrono::steady_clock::now() - start;

    releaseBackend.store(true, std::memory_order_release);
    CHECK(elapsed < 50ms);
}
