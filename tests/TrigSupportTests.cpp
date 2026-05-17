//
// Created by Codex on 2/27/2026.
//

#include "catch.hpp"

#include <cmath>
#include <numbers>

#include "../src/Math/Interval.h"

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

    SECTION("wide sin/cos intervals short-circuit to full range")
    {
        const Interval interval{-100000.0, 100000.0};
        CHECK(sin(interval).lower == -1.0);
        CHECK(sin(interval).upper == 1.0);
        CHECK(cos(interval).lower == -1.0);
        CHECK(cos(interval).upper == 1.0);
    }

    SECTION("tan on finite interval without asymptote")
    {
        const Interval interval{-1.0, 1.0};
        const auto range = tan(interval);

        REQUIRE(std::isfinite(range.lower));
        REQUIRE(std::isfinite(range.upper));
        REQUIRE_FALSE(range.hasDiscontinuity());

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
        REQUIRE(range.hasDiscontinuity());
    }
}
