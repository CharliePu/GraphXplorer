//
// Created by Codex on 2/27/2026.
//

#include "catch.hpp"

#include <cmath>
#include <numbers>

#include "../src/Formula/Formula.h"
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

