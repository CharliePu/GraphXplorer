#include "catch.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../src/Compute/TileRuntime.h"
#include "../src/Formula/FormulaCompiler.h"

namespace
{
using namespace std::chrono_literals;

class RecordingBackend final : public gx::ComputeBackend
{
public:
    [[nodiscard]] gx::BackendCapabilities capabilities() const override
    {
        return {};
    }

    gx::BatchResult classifyIntervals(const gx::IntervalBatchView &batch,
                                      std::span<gx::TileClassificationResult> out) override
    {
        {
            std::lock_guard lock(mutex);
            classifyBatchSizes.push_back(batch.keys.size());
        }
        for (size_t index = 0; index < out.size(); ++index)
        {
            out[index] = {
                batch.keys[index],
                gx::TileClassification::UniformFalse,
                Interval{0.0, 0.0}
            };
        }
        return {true, out.size(), {}};
    }

    gx::BatchResult rasterizeRegions(const gx::RasterBatchView &batch,
                                     std::span<gx::RegionOutput> out) override
    {
        {
            std::lock_guard lock(mutex);
            rasterBatchSizes.push_back(batch.keys.size());
            rasterPixelsPerAxisValues.push_back(batch.pixelsPerAxis);
            rasterAllowGpuValues.push_back(batch.allowGpu);
        }
        for (size_t index = 0; index < out.size(); ++index)
        {
            out[index].key = batch.keys[index];
            out[index].width = batch.pixelsPerAxis;
            out[index].height = batch.pixelsPerAxis;
            out[index].pixels.assign(
                static_cast<size_t>(batch.pixelsPerAxis) * batch.pixelsPerAxis,
                uint8_t{255});
        }
        return {true, out.size(), {}};
    }

    [[nodiscard]] std::vector<size_t> classifySizes() const
    {
        std::lock_guard lock(mutex);
        return classifyBatchSizes;
    }

    [[nodiscard]] std::vector<size_t> rasterSizes() const
    {
        std::lock_guard lock(mutex);
        return rasterBatchSizes;
    }

    [[nodiscard]] std::vector<uint32_t> rasterPixels() const
    {
        std::lock_guard lock(mutex);
        return rasterPixelsPerAxisValues;
    }

    [[nodiscard]] std::vector<bool> rasterAllowGpu() const
    {
        std::lock_guard lock(mutex);
        return rasterAllowGpuValues;
    }

private:
    mutable std::mutex mutex;
    std::vector<size_t> classifyBatchSizes;
    std::vector<size_t> rasterBatchSizes;
    std::vector<uint32_t> rasterPixelsPerAxisValues;
    std::vector<bool> rasterAllowGpuValues;
};

class FailureBackend final : public gx::ComputeBackend
{
public:
    bool failClassify{false};
    bool failRaster{false};
    bool throwRaster{false};

    [[nodiscard]] gx::BackendCapabilities capabilities() const override
    {
        return {};
    }

    gx::BatchResult classifyIntervals(const gx::IntervalBatchView &batch,
                                      std::span<gx::TileClassificationResult> out) override
    {
        if (failClassify)
        {
            return {false, 0, "simulated classify failure"};
        }
        for (size_t index = 0; index < out.size(); ++index)
        {
            out[index] = {
                batch.keys[index],
                gx::TileClassification::Mixed,
                Interval{-1.0, 1.0}
            };
        }
        return {true, out.size(), {}};
    }

    gx::BatchResult rasterizeRegions(const gx::RasterBatchView &batch,
                                     std::span<gx::RegionOutput> out) override
    {
        if (throwRaster)
        {
            throw std::runtime_error{"simulated raster exception"};
        }
        if (failRaster)
        {
            return {false, 0, "simulated raster failure"};
        }
        for (size_t index = 0; index < out.size(); ++index)
        {
            out[index].key = batch.keys[index];
            out[index].width = batch.pixelsPerAxis;
            out[index].height = batch.pixelsPerAxis;
            out[index].pixels.assign(
                static_cast<size_t>(batch.pixelsPerAxis) * batch.pixelsPerAxis,
                uint8_t{255});
        }
        return {true, out.size(), {}};
    }
};

class FixedBatchPolicy final : public gx::BackendBatchPolicy
{
public:
    explicit FixedBatchPolicy(const size_t nextBatchSize): batchSize{nextBatchSize}
    {
    }

    [[nodiscard]] size_t choose(const gx::JobKind kind,
                                const size_t remainingJobs,
                                const uint32_t pixelsPerAxis = 0) override
    {
        (void)kind;
        (void)pixelsPerAxis;
        std::lock_guard lock(mutex);
        ++chooseCalls;
        return std::min(batchSize, remainingJobs);
    }

    void observe(const gx::JobKind kind,
                 const size_t observedBatchSize,
                 const std::chrono::microseconds latency,
                 const uint32_t pixelsPerAxis = 0) override
    {
        (void)kind;
        (void)latency;
        (void)pixelsPerAxis;
        std::lock_guard lock(mutex);
        observedBatchSizes.push_back(observedBatchSize);
    }

    [[nodiscard]] size_t choices() const
    {
        std::lock_guard lock(mutex);
        return chooseCalls;
    }

    [[nodiscard]] std::vector<size_t> observations() const
    {
        std::lock_guard lock(mutex);
        return observedBatchSizes;
    }

private:
    size_t batchSize{1};
    mutable std::mutex mutex;
    size_t chooseCalls{0};
    std::vector<size_t> observedBatchSizes;
};

gx::ViewportRequest requestFor(const gx::CompiledFormula &formula)
{
    return {
        .header = {.requestId = 1, .generation = 1},
        .formula = formula.handle,
        .xRange = Interval{-8.0, 8.0},
        .yRange = Interval{-8.0, 8.0},
        .framebufferWidth = 512,
        .framebufferHeight = 512,
        .devicePixelRatio = 1.0
    };
}

gx::TileJob tileJob(const gx::JobKind kind, const int64_t x)
{
    return {
        .kind = kind,
        .workClass = gx::WorkClass::VisibleNow,
        .key = gx::TileKey{x, 0, 2}
    };
}

gx::TileTransaction mixedNeedsRegionTransaction(const gx::ViewportRequest &request,
                                                const gx::TileKey &key,
                                                const Interval &interval = Interval{-1.0, 1.0})
{
    return {
        .header = request.header,
        .semanticsHash = request.formula.semanticsHash,
        .deltas = {
            gx::TileDelta{
                .header = request.header,
                .semanticsHash = request.formula.semanticsHash,
                .key = key,
                .stage = gx::TileStage::IntervalQueued
            },
            gx::TileDelta{
                .header = request.header,
                .semanticsHash = request.formula.semanticsHash,
                .key = key,
                .stage = gx::TileStage::IntervalReady,
                .classification = gx::TileClassification::Mixed,
                .interval = interval
            },
            gx::TileDelta{
                .header = request.header,
                .semanticsHash = request.formula.semanticsHash,
                .key = key,
                .stage = gx::TileStage::MixedNeedsRegion,
                .classification = gx::TileClassification::Mixed,
                .interval = interval
            }
        }
    };
}

void waitForRuntimeIdle(gx::TileRuntime &runtime)
{
    for (auto spin = 0; spin < 300 && runtime.inFlightCount() > 0; ++spin)
    {
        std::this_thread::sleep_for(1ms);
    }
}
}

TEST_CASE("TileRuntime delegates batch-size decisions to an injected policy",
          "[TileRuntime][Responsiveness]")
{
    auto backend = std::make_unique<RecordingBackend>();
    auto *backendView = backend.get();
    auto policy = std::make_unique<FixedBatchPolicy>(2);
    auto *policyView = policy.get();
    gx::TileRuntime runtime{std::move(backend), 1, gx::TileRuntimeOptions{}, std::move(policy)};
    const auto formula = gx::FormulaCompiler{}.compile("x < y");
    REQUIRE(formula.diagnostics.ok);
    runtime.setLatestRequest(requestFor(formula), formula);

    std::vector<gx::TileJob> jobs;
    for (auto index = int64_t{0}; index < 5; ++index)
    {
        jobs.push_back(tileJob(gx::JobKind::ClassifyInterval, index));
    }
    runtime.submitJobs(jobs);
    waitForRuntimeIdle(runtime);

    CHECK(backendView->classifySizes() == std::vector<size_t>{2, 2, 1});
    CHECK(policyView->choices() == 3);
    CHECK(policyView->observations() == std::vector<size_t>{2, 2, 1});
}

TEST_CASE("TileRuntime splits backend work into bounded sub-batches", "[TileRuntime][Responsiveness]")
{
    auto backend = std::make_unique<RecordingBackend>();
    auto *backendView = backend.get();
    gx::TileRuntime runtime{
        std::move(backend),
        1,
        gx::TileRuntimeOptions{
            .batchOptimizer = {
                .initialIntervalBatchSize = 3,
                .initialRasterBatchSize = 2,
                .maxRasterBatchSize = 8
            }
        }
    };
    const auto formula = gx::FormulaCompiler{}.compile("x < y");
    REQUIRE(formula.diagnostics.ok);
    runtime.setLatestRequest(requestFor(formula), formula);

    std::vector<gx::TileJob> jobs;
    for (auto index = int64_t{0}; index < 7; ++index)
    {
        jobs.push_back(tileJob(gx::JobKind::ClassifyInterval, index));
    }
    for (auto index = int64_t{10}; index < 15; ++index)
    {
        jobs.push_back(tileJob(gx::JobKind::RasterizeRegion, index));
    }
    runtime.submitJobs(jobs);

    for (auto spin = 0; spin < 200 && runtime.inFlightCount() > 0; ++spin)
    {
        std::this_thread::sleep_for(1ms);
    }

    auto classifySizes = backendView->classifySizes();
    auto rasterSizes = backendView->rasterSizes();
    std::ranges::sort(classifySizes);
    std::ranges::sort(rasterSizes);
    REQUIRE(classifySizes == std::vector<size_t>{1, 3, 3});
    REQUIRE(rasterSizes == std::vector<size_t>{1, 2, 2});
}

TEST_CASE("TileRuntime honors completed-work apply budget by deferring work", "[TileRuntime][Responsiveness]")
{
    auto backend = std::make_unique<RecordingBackend>();
    gx::TileRuntime runtime{
        std::move(backend),
        1,
        gx::TileRuntimeOptions{
            .batchOptimizer = {
                .initialIntervalBatchSize = 8,
                .initialRasterBatchSize = 8
            }
        }
    };
    const auto formula = gx::FormulaCompiler{}.compile("x < y");
    REQUIRE(formula.diagnostics.ok);
    runtime.setLatestRequest(requestFor(formula), formula);

    std::vector<gx::TileJob> jobs;
    for (auto index = int64_t{0}; index < 4; ++index)
    {
        jobs.push_back(tileJob(gx::JobKind::ClassifyInterval, index));
    }
    runtime.submitJobs(jobs);

    for (auto spin = 0; spin < 200 && runtime.pendingCompletionCount() == 0; ++spin)
    {
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(runtime.pendingCompletionCount() > 0);

    gx::TileCache cache;
    std::unordered_map<uint64_t, gx::RegionOutput> regions;
    const auto deferred = runtime.drainCompleted(cache, regions, std::chrono::microseconds{-1});
    CHECK(deferred.applied == 0);
    CHECK(runtime.pendingCompletionCount() > 0);

    const auto applied = runtime.drainCompleted(cache, regions, 1s);
    CHECK(applied.applied == 12);
    CHECK(runtime.pendingCompletionCount() == 0);
}

TEST_CASE("TileRuntime notifies when completed work becomes drainable", "[TileRuntime][Responsiveness]")
{
    auto backend = std::make_unique<RecordingBackend>();
    gx::TileRuntime runtime{
        std::move(backend),
        1,
        gx::TileRuntimeOptions{
            .batchOptimizer = {
                .initialIntervalBatchSize = 8
            }
        }
    };

    std::mutex mutex;
    std::condition_variable completed;
    auto wakeCount = 0;
    runtime.setCompletionCallback([&]
    {
        {
            std::lock_guard lock(mutex);
            ++wakeCount;
        }
        completed.notify_one();
    });

    const auto formula = gx::FormulaCompiler{}.compile("x < y");
    REQUIRE(formula.diagnostics.ok);
    runtime.setLatestRequest(requestFor(formula), formula);
    runtime.submitJobs(std::array{tileJob(gx::JobKind::ClassifyInterval, 0)});

    std::unique_lock lock(mutex);
    REQUIRE(completed.wait_for(lock, 1s, [&] { return wakeCount > 0; }));
    lock.unlock();
    CHECK(runtime.pendingCompletionCount() > 0);
}

TEST_CASE("TileRuntime derives default worker count from hardware threads minus headroom",
          "[TileRuntime][Responsiveness]")
{
    CHECK(gx::TileRuntime::recommendedWorkerCount(16, 2) == 14);
    CHECK(gx::TileRuntime::recommendedWorkerCount(2, 2) == 1);
    CHECK(gx::TileRuntime::recommendedWorkerCount(16, 2, 4) == 4);
}

TEST_CASE("TileRuntime passes configured raster sampling resolution to backend",
          "[TileRuntime][Responsiveness]")
{
    auto backend = std::make_unique<RecordingBackend>();
    auto *backendView = backend.get();
    gx::TileRuntime runtime{
        std::move(backend),
        1,
        gx::TileRuntimeOptions{
            .batchOptimizer = {
                .initialRasterBatchSize = 8
            },
            .rasterPixelsPerAxis = 128
        }
    };
    const auto formula = gx::FormulaCompiler{}.compile("x < y");
    REQUIRE(formula.diagnostics.ok);
    runtime.setLatestRequest(requestFor(formula), formula);

    runtime.submitJobs(std::vector<gx::TileJob>{
        tileJob(gx::JobKind::RasterizeRegion, 0),
        tileJob(gx::JobKind::RasterizeRegion, 1)
    });

    for (auto spin = 0; spin < 200 && runtime.inFlightCount() > 0; ++spin)
    {
        std::this_thread::sleep_for(1ms);
    }

    const auto pixels = backendView->rasterPixels();
    REQUIRE_FALSE(pixels.empty());
    CHECK(std::ranges::all_of(pixels, [](const uint32_t value) { return value == 128; }));
}

TEST_CASE("TileRuntime defaults raster sampling resolution to one screen tile",
          "[TileRuntime][Responsiveness]")
{
    auto backend = std::make_unique<RecordingBackend>();
    auto *backendView = backend.get();
    gx::TileRuntime runtime{
        std::move(backend),
        1,
        gx::TileRuntimeOptions{
            .batchOptimizer = {
                .initialRasterBatchSize = 8
            }
        }
    };
    const auto formula = gx::FormulaCompiler{}.compile("x < y");
    REQUIRE(formula.diagnostics.ok);
    runtime.setLatestRequest(requestFor(formula), formula);

    runtime.submitJobs(std::array{tileJob(gx::JobKind::RasterizeRegion, 0)});

    for (auto spin = 0; spin < 200 && runtime.inFlightCount() > 0; ++spin)
    {
        std::this_thread::sleep_for(1ms);
    }

    const auto pixels = backendView->rasterPixels();
    REQUIRE_FALSE(pixels.empty());
    CHECK(pixels.front() == gx::RasterTileScreenPixels);
}

TEST_CASE("TileRuntime propagates GPU raster admission to backend batches",
          "[TileRuntime][Responsiveness]")
{
    auto backend = std::make_unique<RecordingBackend>();
    auto *backendView = backend.get();
    gx::TileRuntime runtime{
        std::move(backend),
        1,
        gx::TileRuntimeOptions{
            .batchOptimizer = {
                .initialRasterBatchSize = 8
            }
        }
    };
    const auto formula = gx::FormulaCompiler{}.compile("x < y");
    REQUIRE(formula.diagnostics.ok);
    runtime.setLatestRequest(requestFor(formula), formula);
    runtime.setGpuRasterAllowed(false);

    runtime.submitJobs(std::vector<gx::TileJob>{
        tileJob(gx::JobKind::RasterizeRegion, 0),
        tileJob(gx::JobKind::RasterizeRegion, 1)
    });

    waitForRuntimeIdle(runtime);

    const auto allowGpu = backendView->rasterAllowGpu();
    REQUIRE_FALSE(allowGpu.empty());
    CHECK(std::ranges::none_of(allowGpu, [](const bool value) { return value; }));
}

TEST_CASE("TileRuntime resets interval queued tiles after backend classification failure",
          "[TileRuntime][Recovery]")
{
    auto backend = std::make_unique<FailureBackend>();
    backend->failClassify = true;
    gx::TileRuntime runtime{
        std::move(backend),
        1,
        gx::TileRuntimeOptions{
            .batchOptimizer = {
                .initialIntervalBatchSize = 8
            }
        }
    };
    const auto formula = gx::FormulaCompiler{}.compile("x < y");
    REQUIRE(formula.diagnostics.ok);
    const auto request = requestFor(formula);
    runtime.setLatestRequest(request, formula);

    auto job = tileJob(gx::JobKind::ClassifyInterval, 0);
    gx::TileCache cache;
    REQUIRE(cache.transition(job.key, request.formula.semanticsHash, gx::TileStage::IntervalQueued));

    runtime.submitJobs(std::array{job});
    waitForRuntimeIdle(runtime);
    REQUIRE(runtime.inFlightCount() == 0);

    std::unordered_map<uint64_t, gx::RegionOutput> regions;
    const auto drained = runtime.drainCompleted(cache, regions, 1s);
    CHECK(drained.applied == 1);
    CHECK(drained.rejected == 0);

    const auto *record = cache.find(job.key, request.formula.semanticsHash);
    REQUIRE(record != nullptr);
    CHECK(record->valueState == gx::TileValueState::Unknown);
    CHECK(record->workState == gx::TileWorkState::Idle);
    CHECK(cache.transition(job.key, request.formula.semanticsHash, gx::TileStage::IntervalQueued));
}

TEST_CASE("TileRuntime resets region queued tiles after raster failure",
          "[TileRuntime][Recovery]")
{
    auto backend = std::make_unique<FailureBackend>();
    backend->failRaster = true;
    gx::TileRuntime runtime{
        std::move(backend),
        1,
        gx::TileRuntimeOptions{
            .batchOptimizer = {
                .initialRasterBatchSize = 8
            }
        }
    };
    const auto formula = gx::FormulaCompiler{}.compile("x < y");
    REQUIRE(formula.diagnostics.ok);
    const auto request = requestFor(formula);
    runtime.setLatestRequest(request, formula);

    auto job = tileJob(gx::JobKind::RasterizeRegion, 0);
    job.dependencies.interval = true;
    job.interval = Interval{-1.0, 1.0};
    gx::TileCache cache;
    REQUIRE(cache.apply(mixedNeedsRegionTransaction(request, job.key)).rejected == 0);
    REQUIRE(cache.transition(job.key, request.formula.semanticsHash, gx::TileStage::RegionQueued));

    runtime.submitJobs(std::array{job});
    waitForRuntimeIdle(runtime);
    REQUIRE(runtime.inFlightCount() == 0);

    std::unordered_map<uint64_t, gx::RegionOutput> regions;
    const auto drained = runtime.drainCompleted(cache, regions, 1s);
    CHECK(drained.applied == 1);
    CHECK(drained.rejected == 0);

    const auto *record = cache.find(job.key, request.formula.semanticsHash);
    REQUIRE(record != nullptr);
    CHECK(record->valueState == gx::TileValueState::Mixed);
    CHECK(record->workState == gx::TileWorkState::Idle);
    REQUIRE(record->interval.has_value());
    CHECK(sameInterval(*record->interval, Interval{-1.0, 1.0}));
    CHECK(cache.transition(job.key, request.formula.semanticsHash, gx::TileStage::RegionQueued));
}

TEST_CASE("TileRuntime cleans up in-flight state and recovers when raster backend throws",
          "[TileRuntime][Recovery]")
{
    auto backend = std::make_unique<FailureBackend>();
    backend->throwRaster = true;
    gx::TileRuntime runtime{
        std::move(backend),
        1,
        gx::TileRuntimeOptions{
            .batchOptimizer = {
                .initialRasterBatchSize = 8
            }
        }
    };
    const auto formula = gx::FormulaCompiler{}.compile("x < y");
    REQUIRE(formula.diagnostics.ok);
    const auto request = requestFor(formula);
    runtime.setLatestRequest(request, formula);

    auto job = tileJob(gx::JobKind::RasterizeRegion, 1);
    job.dependencies.interval = true;
    job.interval = Interval{-1.0, 1.0};
    gx::TileCache cache;
    REQUIRE(cache.apply(mixedNeedsRegionTransaction(request, job.key)).rejected == 0);
    REQUIRE(cache.transition(job.key, request.formula.semanticsHash, gx::TileStage::RegionQueued));

    runtime.submitJobs(std::array{job});
    waitForRuntimeIdle(runtime);
    REQUIRE(runtime.inFlightCount() == 0);

    std::unordered_map<uint64_t, gx::RegionOutput> regions;
    const auto drained = runtime.drainCompleted(cache, regions, 1s);
    CHECK(drained.applied == 1);
    CHECK(drained.rejected == 0);

    const auto *record = cache.find(job.key, request.formula.semanticsHash);
    REQUIRE(record != nullptr);
    CHECK(record->valueState == gx::TileValueState::Mixed);
    CHECK(record->workState == gx::TileWorkState::Idle);
}
