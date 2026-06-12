#include <catch2/catch_test_macros.hpp>

#include "solve/Trace.h"

#include <cmath>

using namespace gxr;

namespace
{
Relation must(const std::string &src)
{
    std::string err;
    auto r = Relation::parse(src, err);
    REQUIRE(r.has_value());
    return std::move(*r);
}
}

TEST_CASE("trace pins the nearest curve point, certified", "[trace]")
{
    EvalScratch s;
    const double wpp = 0.01;

    // generic equality: cursor hovers just off y = sin(x)
    Relation eq = must("y = sin(x)");
    TraceHit h = traceCurve(eq, 1.0, std::sin(1.0) + 0.004, wpp, wpp, s);
    REQUIRE(h.traced);
    REQUIRE(h.certified);
    REQUIRE(std::abs(h.y - std::sin(h.x)) < 1e-7);
    REQUIRE(std::abs(h.x - 1.0) < 5 * wpp);

    // closed inequality: the boundary is the same residual -- including the
    // tile-edge-aligned case whose region raster carries no transition
    Relation ge = must("y >= 0");
    h = traceCurve(ge, 0.3, 0.004, wpp, wpp, s);
    REQUIRE(h.traced);
    REQUIRE(h.certified);
    REQUIRE(std::abs(h.y) < 1e-7);

    // near-VERTICAL curve orientation (circle at its right edge): the
    // vertical-only v1 scan had nothing here; the Newton glide does
    Relation circ = must("x^2 + y^2 <= 25");
    h = traceCurve(circ, 5.02, 0.3, wpp, wpp, s);
    REQUIRE(h.traced);
    REQUIRE(h.certified);
    REQUIRE(std::abs(h.x * h.x + h.y * h.y - 25.0) < 1e-5);

    // pole: hovering beside the asymptote of y = 1/x must not pin a marker
    // onto x = 0 (the horizontal bracket sees the -inf -> +inf sign flip)
    Relation inv = must("y = 1/x");
    h = traceCurve(inv, 0.001, 0.2, wpp, wpp, s);
    REQUIRE_FALSE(h.traced);

    // far from any curve: no hit at all
    h = traceCurve(eq, 40.0, 25.0, wpp, wpp, s);
    REQUIRE_FALSE(h.traced);
}

TEST_CASE("trace is deterministic and certified at deep zoom", "[trace]")
{
    EvalScratch s;
    // wpp ~ 1e-9: the tolerance floor keeps Newton convergent at coordinate
    // scales where a fixed absolute epsilon would be unreachable
    Relation eq = must("y = sin(x)");
    const double wpp = 1e-9;
    const double x0 = 1.0, y0 = std::sin(1.0);
    TraceHit a = traceCurve(eq, x0 + 3e-9, y0 + 4e-9, wpp, wpp, s);
    TraceHit b = traceCurve(eq, x0 + 3e-9, y0 + 4e-9, wpp, wpp, s);
    REQUIRE(a.traced);
    REQUIRE(a.certified);
    REQUIRE(a.x == b.x);
    REQUIRE(a.y == b.y);
    REQUIRE(std::abs(a.y - std::sin(a.x)) < 1e-12);
}

TEST_CASE("trace holds its branch and declines dense fields", "[trace]")
{
    EvalScratch s;
    const double wpp = 0.01;

    // two parallel branches 6 px apart: a fresh search picks the nearer,
    // but a held branch is NEVER hopped on a small cursor move
    Relation two = must("(y - x)(y - x - 0.06) = 0");
    TraceHit fresh = traceCurve(two, 0.0, 0.045, wpp, wpp, s);
    REQUIRE(fresh.traced);
    REQUIRE(std::abs(fresh.y - fresh.x - 0.06) < 1e-5); // nearer: upper branch

    TraceHit held{};
    held.traced = true;
    held.x = 0.0;
    held.y = 0.0; // locked on the LOWER branch
    TraceHit cont = traceCurve(two, 0.0, 0.045, wpp, wpp, s, 26.0, &held);
    REQUIRE(cont.traced);
    REQUIRE(std::abs(cont.y - cont.x) < 1e-5); // stays on its line

    // and the walk follows the branch tangentially toward the cursor
    cont = traceCurve(two, 0.12, 0.13, wpp, wpp, s, 26.0, &held);
    REQUIRE(cont.traced);
    REQUIRE(std::abs(cont.y - cont.x) < 1e-5);
    REQUIRE(cont.x > 0.05); // it moved along the line, not just reprojected

    // a strand thicket (many crossings under the cursor) refuses to trace:
    // the click must remain a pan
    Relation dense = must("y = sin(100 x)");
    TraceHit field = traceCurve(dense, 0.0, 0.3, wpp, wpp, s);
    REQUIRE_FALSE(field.traced);
}
