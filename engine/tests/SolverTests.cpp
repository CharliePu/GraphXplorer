#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "solve/Solver.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace gxr;
using Catch::Approx;

namespace
{
Relation must(const std::string &src)
{
    std::string err;
    auto r = Relation::parse(src, err);
    REQUIRE(r.has_value());
    return std::move(*r);
}

double coverageFraction(const CoverageTile &t)
{
    double sum = 0.0;
    for (float a : t.alpha) sum += a;
    return sum / (static_cast<double>(t.width) * t.height);
}

double coveredWorldArea(const CoverageTile &t, const WorldRect &r)
{
    const double pixelArea = (r.width() / t.width) * (r.height() / t.height);
    double sum = 0.0;
    for (float a : t.alpha) sum += a;
    return sum * pixelArea;
}
}

TEST_CASE("uniform regions are fully covered / empty", "[solver]")
{
    Relation r = must("y > x");
    EvalScratch s;
    SolveParams p;
    // tile entirely above the diagonal
    CoverageTile above = solveTile(r, WorldRect{0, 5, 1, 6}, p, s);
    for (float a : above.alpha) REQUIRE(a == Approx(1.0f));
    // tile entirely below
    CoverageTile below = solveTile(r, WorldRect{5, 0, 6, 1}, p, s);
    for (float a : below.alpha) REQUIRE(a == Approx(0.0f));
}

TEST_CASE("half-plane coverage equals the closed-form area", "[solver]")
{
    Relation r = must("x < 0");
    EvalScratch s;
    SolveParams p;
    p.tilePx = 64;
    // symmetric tile: exactly the left half is covered
    CoverageTile t = solveTile(r, WorldRect{-1, -1, 1, 1}, p, s);
    REQUIRE(coverageFraction(t) == Approx(0.5).margin(0.005));
}

TEST_CASE("disk coverage area equals pi (sub-pixel AA)", "[solver]")
{
    Relation r = must("x^2 + y^2 < 1");
    EvalScratch s;
    SolveParams p;
    p.tilePx = 128;
    p.subBits = 4;
    const WorldRect rect{-1.5, -1.5, 1.5, 1.5};
    CoverageTile t = solveTile(r, rect, p, s);
    REQUIRE(coveredWorldArea(t, rect) == Approx(std::numbers::pi).margin(0.03));
}

TEST_CASE("solving is deterministic (temporal-stability foundation)", "[solver]")
{
    Relation r = must("y > sin(2^x)");
    EvalScratch s;
    SolveParams p;
    const WorldRect rect{0, -2, 4, 2};
    CoverageTile a = solveTile(r, rect, p, s);
    CoverageTile b = solveTile(r, rect, p, s);
    REQUIRE(a.alpha == b.alpha);
}

TEST_CASE("sub-pixel oscillation produces stable gray, never crashes/hangs", "[solver]")
{
    Relation r = must("y > sin(2^x)");
    EvalScratch s;
    SolveParams p;
    p.tilePx = 64;
    p.subBits = 3;
    // large x: 2^x oscillates far faster than one pixel
    const WorldRect rect{18.0, -1.2, 22.0, 1.2};
    CoverageTile t = solveTile(r, rect, p, s);
    // all coverage finite and in range
    int grayPixels = 0;
    for (float a : t.alpha)
    {
        REQUIRE(std::isfinite(a));
        REQUIRE(a >= 0.0f);
        REQUIRE(a <= 1.0f);
        if (a > 0.05f && a < 0.95f) ++grayPixels;
    }
    // the oscillating band must contain genuinely gray (anti-aliased) pixels
    REQUIRE(grayPixels > 0);
}

TEST_CASE("OBJECTIVE 1: oscillation coverage equals the analytic Lebesgue measure", "[solver]")
{
    // For y > sin(t) with t sweeping many periods, the covered fraction at height
    // y is exactly 1/2 + arcsin(y)/pi. At deep zoom 2^x oscillates sub-pixel, so
    // the rendered gray MUST match that measure (not a biased ~0.5).
    Relation r = must("y > sin(2^x)");
    EvalScratch s;
    SolveParams p;
    p.tilePx = 64;
    p.subBits = 4;
    const WorldRect rect{24.0, -1.2, 28.0, 1.2};
    CoverageTile t = solveTile(r, rect, p, s);

    const double wppY = rect.height() / t.height;
    for (int py = 0; py < t.height; ++py)
    {
        const double yc = rect.y0 + (py + 0.5) * wppY;
        if (std::abs(yc) > 0.8) continue; // skip steep arcsin tails near +-1
        // mean coverage across the row (averages out per-column sampling variance)
        double mean = 0.0;
        for (int px = 0; px < t.width; ++px) mean += t.at(px, py);
        mean /= t.width;
        const double expected = 0.5 + std::asin(std::clamp(yc, -1.0, 1.0)) / std::numbers::pi;
        REQUIRE(mean == Approx(expected).margin(0.04));
    }
}

TEST_CASE("explicit-x 1-D path agrees with the general 2-D solver", "[solver]")
{
    Relation r = must("x > sin(y)"); // explicit in x: sideways sine
    EvalScratch s;
    const WorldRect rect{-3, -3, 3, 3};
    SolveParams analytic;
    analytic.tilePx = 64;
    analytic.subBits = 4;
    analytic.analytic = true;
    SolveParams general = analytic;
    general.analytic = false; // force the 2-D subdivision path

    CoverageTile a = solveTile(r, rect, analytic, s);
    CoverageTile b = solveTile(r, rect, general, s);

    double mad = 0.0;
    for (size_t i = 0; i < a.alpha.size(); ++i) mad += std::abs(a.alpha[i] - b.alpha[i]);
    mad /= a.alpha.size();
    REQUIRE(mad < 0.02); // the fast 1-D measure matches the general solver

    // and it actually fills the right half-ish (x>sin(y))
    REQUIRE(a.at(60, 32) == Approx(1.0f).margin(0.05)); // far right: inside
    REQUIRE(a.at(4, 32) == Approx(0.0f).margin(0.05));  // far left: outside
}

TEST_CASE("equality renders a thin, non-vanishing curve", "[solver]")
{
    Relation r = must("y = x");
    EvalScratch s;
    SolveParams p;
    p.tilePx = 64;
    const WorldRect rect{-1, -1, 1, 1};
    CoverageTile t = solveTile(r, rect, p, s);

    // some pixels are lit (curve visible)
    double total = 0.0;
    for (float a : t.alpha) total += a;
    REQUIRE(total > 0.0);

    // the curve is thin: covered area is a small fraction of the tile
    REQUIRE(coverageFraction(t) < 0.1);

    // a pixel far from the diagonal is dark
    REQUIRE(t.at(56, 4) == Approx(0.0f).margin(1e-3));
}
