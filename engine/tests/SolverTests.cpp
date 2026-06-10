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

TEST_CASE("classifyRegion proves uniform regions and detects boundaries (greedy)", "[solver][greedy]")
{
    EvalScratch s;
    auto cls = [&](const std::string &src, WorldRect r) {
        std::string err;
        auto rel = Relation::parse(src, err);
        REQUIRE(rel.has_value());
        return classifyRegion(*rel, r, s);
    };

    // always-false collapses to one uniform tile over any region
    REQUIRE(cls("x > x + 1", WorldRect{-1000, -1000, 1000, 1000}) == NodeClass::UniformFalse);

    // disk: interior proven true, exterior proven false, straddle is mixed
    REQUIRE(cls("x^2 + y^2 < 1", WorldRect{-0.1, -0.1, 0.1, 0.1}) == NodeClass::UniformTrue);
    REQUIRE(cls("x^2 + y^2 < 1", WorldRect{3, 3, 4, 4}) == NodeClass::UniformFalse);
    REQUIRE(cls("x^2 + y^2 < 1", WorldRect{0.5, 0.5, 0.95, 0.95}) == NodeClass::Mixed);

    // half-plane
    REQUIRE(cls("y > x", WorldRect{0, 5, 1, 6}) == NodeClass::UniformTrue);
    REQUIRE(cls("y > x", WorldRect{5, 0, 6, 1}) == NodeClass::UniformFalse);
    REQUIRE(cls("y > x", WorldRect{0, 0, 2, 2}) == NodeClass::Mixed);

    // equality off the curve is uniformly empty; on the curve is mixed
    REQUIRE(cls("y = x^2", WorldRect{3, 0, 4, 1}) == NodeClass::UniformFalse);
    REQUIRE(cls("y = x^2", WorldRect{-0.5, -0.5, 0.5, 0.5}) == NodeClass::Mixed);
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

TEST_CASE("equality band hugs the analytic curve (parabola)", "[solver]")
{
    Relation r = must("y = x^2");
    EvalScratch s;
    SolveParams p;
    p.tilePx = 64;
    const WorldRect rect{-1.0, -0.5, 1.0, 1.5};
    CoverageTile t = solveTile(r, rect, p, s);

    // every strongly-lit pixel center lies within ~1.5px of the curve, and
    // pixels ON the curve are lit
    const double pw = rect.width() / t.width, ph = rect.height() / t.height;
    for (int py = 0; py < t.height; ++py)
        for (int px = 0; px < t.width; ++px)
        {
            const double wx = rect.x0 + (px + 0.5) * pw;
            const double wy = rect.y0 + (py + 0.5) * ph;
            const double distY = std::abs(wy - wx * wx);
            if (t.at(px, py) > 0.6) REQUIRE(distY < 3.0 * ph);
        }
    int onCurveLit = 0, onCurve = 0;
    for (int px = 4; px < t.width - 4; ++px)
    {
        const double wx = rect.x0 + (px + 0.5) * pw;
        const int py = static_cast<int>((wx * wx - rect.y0) / ph);
        if (py < 1 || py >= t.height - 1) continue;
        ++onCurve;
        if (t.at(px, py) > 0.25 || t.at(px, py - 1) > 0.25 || t.at(px, py + 1) > 0.25)
            ++onCurveLit;
    }
    REQUIRE(onCurve > 30);
    REQUIRE(onCurveLit == onCurve); // no gaps along the curve
}

TEST_CASE("a pole is not a curve: y = 1/x emits no asymptote artifacts", "[solver][marching]")
{
    Relation r = must("y = 1/x");
    EvalScratch s;
    SolveParams p;
    p.tilePx = 64;
    const WorldRect rect{-1.0, -8.0, 1.0, 8.0}; // pole column x=0 crosses the tile
    CoverageTile t = solveTile(r, rect, p, s);

    // both hyperbola branches are present
    double total = 0.0;
    for (float v : t.alpha) total += v;
    REQUIRE(total > 20.0);

    // the band raster is dark down the pole column (away from the curve):
    // at x ~ 0 the real curve needs |y| >= 66, far outside this tile
    for (int py = 8; py < 56; py += 4)
    {
        REQUIRE(t.at(31, py) == Approx(0.0f).margin(5e-2));
        REQUIRE(t.at(32, py) == Approx(0.0f).margin(5e-2));
    }
}

TEST_CASE("a sub-pixel-dense curve family hands off strokes to the band raster",
          "[solver][marching]")
{
    // sin(x*y) = 0 around x~100: curve spacing ~2 per pixel here -- strokes
    // would be pure noise at enormous cost, the band carries the density.
    Relation r = must("sin(x*y) = 0");
    EvalScratch s;
    SolveParams p;
    p.tilePx = 64;
    const WorldRect rect{100.0, 100.0, 108.0, 108.0};
    CoverageTile t = solveTile(r, rect, p, s);
    // strands here are ~4x denser than pixels: nearly EVERY pixel contains
    // curve, so the band must be a near-solid wash. (The old gradient-midpoint
    // distance estimate collapsed in this regime -- interval gradient mids ~0
    // -- and rendered random darkness instead.)
    REQUIRE(coverageFraction(t) > 0.85);

    // a SPARSE view of the same relation renders THIN curves, not a wash
    const WorldRect sparse{0.5, 0.5, 4.5, 4.5};
    CoverageTile t2 = solveTile(r, sparse, p, s);
    REQUIRE(coverageFraction(t2) < 0.30);
}

TEST_CASE("grid-resonant density (~1 strand/pixel) renders the honest wash",
          "[solver]")
{
    // At |x| or |y| ~ 2pi/wpp the family oscillates ~once per pixel -- the
    // strobe blind spot that once produced aliased "light patch" strokes.
    // Band-primary rendering must show the near-solid truth here (the numpy
    // audit measured 99.8%% true curve pixels in this region).
    Relation r = must("sin(x)*sin(y) = sin(x*y)");
    EvalScratch s;
    SolveParams p;
    p.tilePx = 64;
    const WorldRect rect{48.0, 48.0, 56.0, 56.0}; // wpp 0.125: 2pi/wpp ~ 50.3
    CoverageTile t = solveTile(r, rect, p, s);
    REQUIRE(coverageFraction(t) > 0.9);
}
