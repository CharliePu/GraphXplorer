#include <catch2/catch_test_macros.hpp>

#include "math/Interval.h"

#include <cmath>

using gxr::Interval;

namespace
{
// A sound enclosure must contain the true value of the expression at sample points.
bool encloses(const Interval &iv, double trueValue)
{
    return !iv.undef && iv.lo <= trueValue && trueValue <= iv.hi;
}
}

TEST_CASE("interval arithmetic is a sound enclosure", "[interval]")
{
    const Interval x{1.0, 2.0};
    const Interval y{3.0, 5.0};

    const Interval sum = x + y;
    REQUIRE(encloses(sum, 1.0 + 3.0));
    REQUIRE(encloses(sum, 2.0 + 5.0));
    REQUIRE(sum.lo <= 4.0);
    REQUIRE(sum.hi >= 7.0);

    const Interval diff = x - y; // [1-5, 2-3] = [-4, -1]
    REQUIRE(encloses(diff, -4.0));
    REQUIRE(encloses(diff, -1.0));

    const Interval prod = x * y; // [3, 10]
    REQUIRE(encloses(prod, 3.0));
    REQUIRE(encloses(prod, 10.0));
}

TEST_CASE("dependency: x-x straddles zero but is sound", "[interval]")
{
    const Interval x{-1.0, 1.0};
    const Interval d = x - x; // naive interval -> [-2,2], contains 0
    REQUIRE(d.straddlesZero());
    REQUIRE(encloses(d, 0.0));
}

TEST_CASE("division by an interval containing zero yields a discontinuous whole line", "[interval]")
{
    const Interval num{1.0, 2.0};
    const Interval den{-1.0, 1.0};
    const Interval q = num / den;
    REQUIRE(q.disc);
    REQUIRE(std::isinf(q.lo));
    REQUIRE(std::isinf(q.hi));

    const Interval zero{0.0, 0.0};
    REQUIRE((num / zero).undef);
}

TEST_CASE("sqrt and log enforce domains", "[interval]")
{
    REQUIRE(gxr::sqrt(Interval{-4.0, -1.0}).undef);
    const Interval s = gxr::sqrt(Interval{-1.0, 4.0});
    REQUIRE(s.disc);            // domain edge crossed
    REQUIRE(s.lo == 0.0);
    REQUIRE(encloses(s, 2.0));

    REQUIRE(gxr::log(Interval{-2.0, -1.0}).undef);
    REQUIRE(gxr::log(Interval{-1.0, 1.0}).disc);
}

TEST_CASE("sin encloses true values and saturates over wide intervals", "[interval]")
{
    const Interval narrow = gxr::sin(Interval{0.0, 1.0});
    REQUIRE(encloses(narrow, std::sin(0.0)));
    REQUIRE(encloses(narrow, std::sin(1.0)));
    REQUIRE(narrow.hi <= 1.0);

    // crosses pi/2 -> upper must reach 1
    const Interval overPeak = gxr::sin(Interval{1.0, 2.0});
    REQUIRE(overPeak.hi == 1.0);

    // very wide -> full range
    const Interval wide = gxr::sin(Interval{0.0, 100.0});
    REQUIRE(wide.lo == -1.0);
    REQUIRE(wide.hi == 1.0);

    // huge argument: range-reduction unreliable -> conservatively [-1,1]
    const Interval huge = gxr::sin(Interval{1e16, 1e16 + 0.1});
    REQUIRE(huge.lo == -1.0);
    REQUIRE(huge.hi == 1.0);
}

TEST_CASE("tan reports poles as discontinuous whole line", "[interval]")
{
    const Interval pole = gxr::tan(Interval{1.5, 1.7}); // pi/2 ~ 1.5708 inside
    REQUIRE(pole.disc);
    REQUIRE(std::isinf(pole.lo));
}

TEST_CASE("truth comparisons certify the unambiguous cases", "[interval]")
{
    using gxr::cmpGreater;
    using gxr::cmpLess;
    // [3,4] > 0 is certainly true
    const Interval t = cmpGreater(Interval{3.0, 4.0}, Interval{0.0, 0.0});
    REQUIRE(t.lo == 1.0);
    REQUIRE(t.hi == 1.0);
    // [-1,1] > 0 is unknown
    const Interval u = cmpGreater(Interval{-1.0, 1.0}, Interval{0.0, 0.0});
    REQUIRE(u.lo == 0.0);
    REQUIRE(u.hi == 1.0);
    // [-4,-1] > 0 is certainly false
    const Interval f = cmpGreater(Interval{-4.0, -1.0}, Interval{0.0, 0.0});
    REQUIRE(f.lo == 0.0);
    REQUIRE(f.hi == 0.0);
}

TEST_CASE("inverse trig: enclosure, domains, monotonicity", "[interval]")
{
    // atan: defined everywhere, increasing, bounded
    const Interval at = gxr::atan(Interval{-1.0, 1.0});
    REQUIRE(encloses(at, std::atan(-1.0)));
    REQUIRE(encloses(at, std::atan(1.0)));
    REQUIRE(at.lo <= -0.78);
    REQUIRE(at.hi >= 0.78);

    // asin: increasing on [-1,1]
    const Interval as = gxr::asin(Interval{0.0, 0.5});
    REQUIRE(encloses(as, std::asin(0.0)));
    REQUIRE(encloses(as, std::asin(0.5)));

    // acos: decreasing on [-1,1]
    const Interval ac = gxr::acos(Interval{0.0, 0.5});
    REQUIRE(encloses(ac, std::acos(0.0)));
    REQUIRE(encloses(ac, std::acos(0.5)));
    REQUIRE(ac.lo <= ac.hi);

    // domain handling
    REQUIRE(gxr::asin(Interval{2.0, 3.0}).undef);          // entirely outside [-1,1]
    REQUIRE(gxr::asin(Interval{0.5, 2.0}).disc);           // crosses the domain edge
    REQUIRE_FALSE(gxr::atan(Interval{-1e9, 1e9}).undef);   // atan never undefined
}

TEST_CASE("ipow handles even/odd and zero crossing", "[interval]")
{
    const Interval sq = gxr::ipow(Interval{-2.0, 3.0}, 2); // [0, 9]
    REQUIRE(sq.lo == 0.0);
    REQUIRE(encloses(sq, 9.0));
    REQUIRE(encloses(sq, 4.0));

    const Interval cube = gxr::ipow(Interval{-2.0, 3.0}, 3); // [-8, 27]
    REQUIRE(encloses(cube, -8.0));
    REQUIRE(encloses(cube, 27.0));
}
