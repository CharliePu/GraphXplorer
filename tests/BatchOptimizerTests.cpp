#include "catch.hpp"

#include "../src/Compute/BatchOptimizer.h"

TEST_CASE("BatchOptimizer chooses the highest-throughput feasible frontier point", "[BatchOptimizer]")
{
    gx::BatchOptimizer optimizer{gx::BatchOptimizerOptions{
        .targetBatchLatency = std::chrono::microseconds{100000},
        .initialIntervalBatchSize = 4,
        .initialRasterBatchSize = 2,
        .maxRasterBatchSize = 32
    }};

    CHECK(optimizer.choose(gx::JobKind::RasterizeRegion, 100) == 2);

    optimizer.observe(gx::JobKind::RasterizeRegion, 4, std::chrono::microseconds{80000});
    optimizer.observe(gx::JobKind::RasterizeRegion, 8, std::chrono::microseconds{90000});
    optimizer.observe(gx::JobKind::RasterizeRegion, 16, std::chrono::microseconds{160000});

    CHECK(optimizer.choose(gx::JobKind::RasterizeRegion, 100) == 8);
    CHECK(optimizer.choose(gx::JobKind::RasterizeRegion, 6) == 4);
}

TEST_CASE("BatchOptimizer prunes dominated candidates", "[BatchOptimizer]")
{
    gx::BatchOptimizer optimizer{gx::BatchOptimizerOptions{
        .targetBatchLatency = std::chrono::microseconds{100000}
    }};

    optimizer.observe(gx::JobKind::ClassifyInterval, 4, std::chrono::microseconds{80000});
    optimizer.observe(gx::JobKind::ClassifyInterval, 4, std::chrono::microseconds{40000});
    optimizer.observe(gx::JobKind::ClassifyInterval, 2, std::chrono::microseconds{50000});

    const auto frontier = optimizer.frontier(gx::JobKind::ClassifyInterval);
    REQUIRE(frontier.size() == 1);
    CHECK(frontier.front().batchSize == 4);
    CHECK(frontier.front().latency == std::chrono::microseconds{40000});
}

TEST_CASE("BatchOptimizer explores larger batches while latency has headroom", "[BatchOptimizer]")
{
    gx::BatchOptimizer optimizer{gx::BatchOptimizerOptions{
        .targetBatchLatency = std::chrono::microseconds{100000},
        .initialIntervalBatchSize = 4,
        .maxIntervalBatchSize = 64
    }};

    CHECK(optimizer.choose(gx::JobKind::ClassifyInterval, 128) == 4);

    optimizer.observe(gx::JobKind::ClassifyInterval, 4, std::chrono::microseconds{20000});
    CHECK(optimizer.choose(gx::JobKind::ClassifyInterval, 128) == 8);

    optimizer.observe(gx::JobKind::ClassifyInterval, 8, std::chrono::microseconds{45000});
    CHECK(optimizer.choose(gx::JobKind::ClassifyInterval, 128) == 16);

    optimizer.observe(gx::JobKind::ClassifyInterval, 16, std::chrono::microseconds{140000});
    CHECK(optimizer.choose(gx::JobKind::ClassifyInterval, 128) == 12);
}
