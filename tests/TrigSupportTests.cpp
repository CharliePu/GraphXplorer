//
// Created by Codex on 2/27/2026.
//

#include "catch.hpp"

#include <array>
#include <cmath>
#include <numbers>

#include "../src/Formula/Formula.h"
#include "../src/Graph/Graph.h"
#include "../src/Math/CpuChunkRenderer.h"
#include "../src/Math/Interval.h"
#include "../src/Math/OpenCLChunkRenderer.h"

namespace
{
bool containsWithTolerance(const Interval &interval, const double value, const double epsilon = 1e-6)
{
    return value >= interval.lower - epsilon && value <= interval.upper + epsilon;
}
}

TEST_CASE("Interval trig functions conservatively bound sampled values", "[Interval][Trig]")
{
    constexpr auto pi = std::numbers::pi_v<double>;

    SECTION("sin on [0, pi]")
    {
        const Interval interval{0.0, pi};
        const auto range = sin(interval);

        REQUIRE(range.lower <= 0.0 + 1e-6);
        REQUIRE(range.upper >= 1.0 - 1e-6);

        for (int i = 0; i <= 1000; ++i)
        {
            const auto x = interval.lower + interval.size() * (static_cast<double>(i) / 1000.0);
            REQUIRE(containsWithTolerance(range, std::sin(x)));
        }
    }

    SECTION("cos on [0, 2pi]")
    {
        const Interval interval{0.0, 2.0 * pi};
        const auto range = cos(interval);

        REQUIRE(range.lower <= -1.0 + 1e-6);
        REQUIRE(range.upper >= 1.0 - 1e-6);

        for (int i = 0; i <= 1000; ++i)
        {
            const auto x = interval.lower + interval.size() * (static_cast<double>(i) / 1000.0);
            REQUIRE(containsWithTolerance(range, std::cos(x)));
        }
    }

    SECTION("tan on finite interval without asymptote")
    {
        const Interval interval{-1.0, 1.0};
        const auto range = tan(interval);

        REQUIRE(std::isfinite(range.lower));
        REQUIRE(std::isfinite(range.upper));

        for (int i = 0; i <= 1000; ++i)
        {
            const auto x = interval.lower + interval.size() * (static_cast<double>(i) / 1000.0);
            REQUIRE(containsWithTolerance(range, std::tan(x)));
        }
    }

    SECTION("tan crossing asymptote returns infinite bounds")
    {
        const Interval interval{1.4, 1.8};
        const auto range = tan(interval);
        REQUIRE(std::isinf(range.lower));
        REQUIRE(std::isinf(range.upper));
        REQUIRE(range.lower < 0.0);
        REQUIRE(range.upper > 0.0);
    }
}

TEST_CASE("OpenCL trig rasterization agrees with CPU renderer", "[OpenCL][Trig]")
{
    OpenCLChunkRenderer openclRenderer;
    if (!openclRenderer.isAvailable())
    {
        SUCCEED("OpenCL device not available; CPU fallback path covers rendering.");
        return;
    }

    CpuChunkRenderer cpuRenderer;
    constexpr int textureSize = 64;

    const auto compareFormula = [&](const std::string &formulaText, const Interval &xRange, const Interval &yRange)
    {
        const auto formula = std::make_shared<Formula>(formulaText);

        std::vector<int> cpuPixels;
        std::vector<int> gpuPixels;

        REQUIRE(cpuRenderer.rasterizeMixedChunkTexture(formula, xRange, yRange, textureSize, cpuPixels));
        REQUIRE(openclRenderer.rasterizeMixedChunkTexture(formula, xRange, yRange, textureSize, gpuPixels));
        REQUIRE(cpuPixels.size() == gpuPixels.size());

        size_t mismatchCount = 0;
        for (size_t i = 0; i < cpuPixels.size(); ++i)
        {
            if (cpuPixels[i] != gpuPixels[i])
            {
                ++mismatchCount;
            }
        }

        const auto mismatchRatio = static_cast<double>(mismatchCount) / static_cast<double>(cpuPixels.size());
        REQUIRE(mismatchRatio <= 0.01);
    };

    compareFormula("sin(x)+0.7cos(y)>0.12", {-2.5, 2.5}, {-2.5, 2.5});
    compareFormula("tan(x)<0.5", {-1.0, 1.0}, {-1.0, 1.0});
}

TEST_CASE("OpenCL contour marching agrees with CPU renderer on a linear residual", "[OpenCL][Contour]")
{
    OpenCLChunkRenderer openclRenderer;
    if (!openclRenderer.isAvailable())
    {
        SUCCEED("OpenCL device not available; contour path falls back to CPU.");
        return;
    }

    CpuChunkRenderer cpuRenderer;
    const Formula residualFormula("x-y");
    constexpr auto cellsPerAxis = 32;
    const Interval xRange{0.0, 1.0};
    const Interval yRange{0.0, 1.0};

    std::vector<RasterContourSegment> cpuSegments;
    std::vector<RasterContourSegment> gpuSegments;
    REQUIRE(cpuRenderer.rasterizeChunkContourSegments(residualFormula.getRPN(), xRange, yRange, cellsPerAxis, cpuSegments));
    REQUIRE(openclRenderer.rasterizeChunkContourSegments(
        residualFormula.getRPN(), xRange, yRange, cellsPerAxis, gpuSegments));

    REQUIRE(cpuSegments.size() == gpuSegments.size());
    constexpr auto epsilon = 1e-5;

    for (size_t i = 0; i < cpuSegments.size(); ++i)
    {
        CHECK(std::abs(cpuSegments[i].x0 - gpuSegments[i].x0) <= epsilon);
        CHECK(std::abs(cpuSegments[i].y0 - gpuSegments[i].y0) <= epsilon);
        CHECK(std::abs(cpuSegments[i].x1 - gpuSegments[i].x1) <= epsilon);
        CHECK(std::abs(cpuSegments[i].y1 - gpuSegments[i].y1) <= epsilon);
    }
}

TEST_CASE("OpenCL contour endpoints stay within chunk bounds for linear residual across far chunks",
          "[OpenCL][Contour][Bounds]")
{
    OpenCLChunkRenderer openclRenderer;
    if (!openclRenderer.isAvailable())
    {
        SUCCEED("OpenCL backend unavailable on this machine");
        return;
    }

    Formula residualFormula{"x-y"};
    constexpr auto cellsPerAxis = 32;
    constexpr auto epsilon = 1e-3;

    const std::array<std::pair<int64_t, int>, 8> chunkCases{{
        {0, 0},
        {1, 0},
        {-1, 0},
        {1024, 0},
        {-1024, 0},
        {1024, 10},
        {-1024, 10},
        {1LL << 20, 8}
    }};

    for (const auto &[chunkIndex, level] : chunkCases)
    {
        const auto xRange = chunkIndexToRange(chunkIndex, level);
        const auto yRange = chunkIndexToRange(chunkIndex + 1, level);

        std::vector<RasterContourSegment> segments;
        REQUIRE(openclRenderer.rasterizeChunkContourSegments(
            residualFormula.getRPN(), xRange, yRange, cellsPerAxis, segments));

        for (const auto &segment : segments)
        {
            INFO("chunkIndex=" << chunkIndex << " level=" << level);
            CHECK(std::isfinite(segment.x0));
            CHECK(std::isfinite(segment.y0));
            CHECK(std::isfinite(segment.x1));
            CHECK(std::isfinite(segment.y1));

            CHECK(segment.x0 >= xRange.lower - epsilon);
            CHECK(segment.x0 <= xRange.upper + epsilon);
            CHECK(segment.x1 >= xRange.lower - epsilon);
            CHECK(segment.x1 <= xRange.upper + epsilon);
            CHECK(segment.y0 >= yRange.lower - epsilon);
            CHECK(segment.y0 <= yRange.upper + epsilon);
            CHECK(segment.y1 >= yRange.lower - epsilon);
            CHECK(segment.y1 <= yRange.upper + epsilon);
        }
    }
}
