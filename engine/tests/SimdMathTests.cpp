#include <catch2/catch_test_macros.hpp>

#include "expr/Relation.h"
#include "math/Simd.h"

#include <cmath>
#include <cstdint>
#include <string>

using namespace gxr;

namespace
{
// deterministic splitmix64 -> double in [0,1)
struct Rng
{
    uint64_t s{0x9e3779b97f4a7c15ULL};
    double next01()
    {
        s += 0x9e3779b97f4a7c15ULL;
        uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        z ^= z >> 31;
        return static_cast<double>(z >> 11) * 0x1.0p-53;
    }
};

double relErr(double got, double want)
{
    if (got == want) return 0.0;
    const double scale = std::max(std::abs(want), 1e-300);
    return std::abs(got - want) / scale;
}
}

TEST_CASE("simd exp2 matches the CRT to a few ulp across the full range", "[simd]")
{
    Rng rng;
    double worst = 0.0;
    for (int it = 0; it < 4000; ++it)
    {
        vd x;
        for (int i = 0; i < kLanes; ++i) x.v[i] = (rng.next01() * 2.0 - 1.0) * 1000.0;
        const vd r = vexp2(x);
        for (int i = 0; i < kLanes; ++i) worst = std::max(worst, relErr(r.v[i], std::exp2(x.v[i])));
    }
    REQUIRE(worst < 1e-14);

    // integer exponents are EXACT (r=0 path)
    vd xi{};
    for (int i = 0; i < kLanes; ++i) xi.v[i] = static_cast<double>(i * 100 - 300);
    const vd ri = vexp2(xi);
    for (int i = 0; i < kLanes; ++i) REQUIRE(ri.v[i] == std::exp2(xi.v[i]));

    // specials
    vd sp{};
    sp.v[0] = std::nan("");
    sp.v[1] = 2000.0;
    sp.v[2] = -2000.0;
    sp.v[3] = 0.0;
    sp.v[4] = 1024.5; // near overflow, still finite-representable scale path
    sp.v[5] = -1074.0; // deepest subnormal
    sp.v[6] = 1.0;
    sp.v[7] = -1.0;
    const vd rs = vexp2(sp);
    REQUIRE(std::isnan(rs.v[0]));
    REQUIRE(std::isinf(rs.v[1]));
    REQUIRE(rs.v[2] == 0.0);
    REQUIRE(rs.v[3] == 1.0);
    REQUIRE(relErr(rs.v[4], std::exp2(1024.5)) < 1e-14);
    REQUIRE(relErr(rs.v[5], std::exp2(-1074.0)) < 0.5); // subnormal: order-of-magnitude
    REQUIRE(rs.v[6] == 2.0);
    REQUIRE(rs.v[7] == 0.5);
}

TEST_CASE("simd exp matches the CRT to a few ulp", "[simd]")
{
    Rng rng;
    double worst = 0.0;
    for (int it = 0; it < 4000; ++it)
    {
        vd x;
        for (int i = 0; i < kLanes; ++i) x.v[i] = (rng.next01() * 2.0 - 1.0) * 700.0;
        const vd r = vexp(x);
        for (int i = 0; i < kLanes; ++i) worst = std::max(worst, relErr(r.v[i], std::exp(x.v[i])));
    }
    REQUIRE(worst < 2e-14);
}

TEST_CASE("simd sincos matches the CRT across magnitudes up to 2^44", "[simd]")
{
    Rng rng;
    double worstS = 0.0, worstC = 0.0;
    for (int mag = -3; mag <= 44; ++mag)
    {
        const double scale = std::ldexp(1.0, mag);
        for (int it = 0; it < 200; ++it)
        {
            vd x, s, c;
            for (int i = 0; i < kLanes; ++i)
            {
                const double sign = rng.next01() < 0.5 ? -1.0 : 1.0;
                x.v[i] = sign * (0.5 + rng.next01()) * scale;
            }
            vsincos(x, s, c);
            for (int i = 0; i < kLanes; ++i)
            {
                worstS = std::max(worstS, std::abs(s.v[i] - std::sin(x.v[i])));
                worstC = std::max(worstC, std::abs(c.v[i] - std::cos(x.v[i])));
            }
        }
    }
    // values live in [-1,1]; a few-ulp absolute tolerance
    REQUIRE(worstS < 5e-15);
    REQUIRE(worstC < 5e-15);
}

TEST_CASE("simd sincos huge/non-finite lanes defer to the CRT exactly", "[simd]")
{
    vd x{}, s{}, c{};
    x.v[0] = 1e200;
    x.v[1] = -3.7e15;
    x.v[2] = std::numeric_limits<double>::infinity();
    x.v[3] = std::nan("");
    x.v[4] = 1.0;
    x.v[5] = 1e308;
    x.v[6] = -1e16;
    x.v[7] = 0.0;
    vsincos(x, s, c);
    for (int i : {0, 1, 5, 6})
    {
        REQUIRE(s.v[i] == std::sin(x.v[i]));
        REQUIRE(c.v[i] == std::cos(x.v[i]));
    }
    REQUIRE(std::isnan(s.v[2]));
    REQUIRE(std::isnan(s.v[3]));
    REQUIRE(s.v[7] == 0.0);
    REQUIRE(c.v[7] == 1.0);
}

TEST_CASE("batched pointInsideCount matches the scalar pointInside", "[simd]")
{
    const char *rels[] = {
        "y > sin(2^x) + y*0", "sin(x*y) > 0",      "x^2 + y^2 < 1", "y >= exp(x) - 2",
        "tan(x) > y",         "y = x^2",            "x > 1 && y < 2", "y < log(abs(x) + 1)",
    };
    Rng rng;
    EvalScratch s;
    for (const char *src : rels)
    {
        std::string err;
        auto rel = Relation::parse(src, err);
        REQUIRE(rel.has_value());
        // 257 points exercises the 256-wide chunk boundary
        constexpr int n = 257;
        double xs[n], ys[n];
        for (int trial = 0; trial < 4; ++trial)
        {
            for (int i = 0; i < n; ++i)
            {
                xs[i] = (rng.next01() * 2.0 - 1.0) * 30.0;
                ys[i] = (rng.next01() * 2.0 - 1.0) * 30.0;
            }
            int scalarHits = 0;
            for (int i = 0; i < n; ++i) scalarHits += rel->pointInside(xs[i], ys[i], s);
            const int batchHits = rel->pointInsideCount(xs, ys, n, s);
            REQUIRE(batchHits == scalarHits);
        }
    }
}
