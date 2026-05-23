#include "catch.hpp"

#include "../src/Compute/BatchOptimizer.h"

TEST_CASE("BatchOptimizer chooses the highest-throughput observed batch", "[BatchOptimizer]")
{
    gx::BatchOptimizer optimizer{gx::BatchOptimizerOptions{
        .initialRasterBatchSize = 2,
        .maxRasterBatchSize = 32,
        .maxRasterBatchLatency = std::chrono::microseconds{0}
    }};

    CHECK(optimizer.choose(gx::JobKind::RasterizeRegion, 100) == 2);

    optimizer.observe(gx::JobKind::RasterizeRegion, 4, std::chrono::microseconds{80000});
    optimizer.observe(gx::JobKind::RasterizeRegion, 8, std::chrono::microseconds{90000});
    optimizer.observe(gx::JobKind::RasterizeRegion, 16, std::chrono::microseconds{160000});

    CHECK(optimizer.choose(gx::JobKind::RasterizeRegion, 100) == 32);
    CHECK(optimizer.choose(gx::JobKind::RasterizeRegion, 6) == 6);
}

TEST_CASE("BatchOptimizer caps raster exploration by latency budget", "[BatchOptimizer][Responsiveness]")
{
    gx::BatchOptimizer optimizer{gx::BatchOptimizerOptions{
        .initialRasterBatchSize = 1,
        .maxRasterBatchSize = 64,
        .maxRasterBatchLatency = std::chrono::microseconds{8000}
    }};

    CHECK(optimizer.choose(gx::JobKind::RasterizeRegion, 100) == 1);

    optimizer.observe(gx::JobKind::RasterizeRegion, 1, std::chrono::microseconds{4000});
    CHECK(optimizer.choose(gx::JobKind::RasterizeRegion, 100) == 2);

    optimizer.observe(gx::JobKind::RasterizeRegion, 2, std::chrono::microseconds{6500});
    CHECK(optimizer.choose(gx::JobKind::RasterizeRegion, 100) == 2);

    optimizer.observe(gx::JobKind::RasterizeRegion, 4, std::chrono::microseconds{20000});
    CHECK(optimizer.choose(gx::JobKind::RasterizeRegion, 100) == 2);
}

TEST_CASE("BatchOptimizer prunes throughput-dominated candidates", "[BatchOptimizer]")
{
    gx::BatchOptimizer optimizer;

    optimizer.observe(gx::JobKind::ClassifyInterval, 4, std::chrono::microseconds{80000});
    optimizer.observe(gx::JobKind::ClassifyInterval, 4, std::chrono::microseconds{40000});
    optimizer.observe(gx::JobKind::ClassifyInterval, 2, std::chrono::microseconds{50000});

    const auto frontier = optimizer.frontier(gx::JobKind::ClassifyInterval);
    REQUIRE(frontier.size() == 1);
    CHECK(frontier.front().batchSize == 4);
    CHECK(frontier.front().latency == std::chrono::microseconds{40000});
}

TEST_CASE("BatchOptimizer explores larger batches only from the best frontier edge", "[BatchOptimizer]")
{
    gx::BatchOptimizer optimizer{gx::BatchOptimizerOptions{
        .initialIntervalBatchSize = 4,
        .maxIntervalBatchSize = 64
    }};

    CHECK(optimizer.choose(gx::JobKind::ClassifyInterval, 128) == 4);

    optimizer.observe(gx::JobKind::ClassifyInterval, 4, std::chrono::microseconds{20000});
    CHECK(optimizer.choose(gx::JobKind::ClassifyInterval, 128) == 8);

    optimizer.observe(gx::JobKind::ClassifyInterval, 8, std::chrono::microseconds{30000});
    CHECK(optimizer.choose(gx::JobKind::ClassifyInterval, 128) == 16);

    optimizer.observe(gx::JobKind::ClassifyInterval, 16, std::chrono::microseconds{90000});
    CHECK(optimizer.choose(gx::JobKind::ClassifyInterval, 128) == 8);
}
