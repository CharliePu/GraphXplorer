#ifndef GXR_MATH_INTERVAL_H
#define GXR_MATH_INTERVAL_H

#include <cmath>
#include <limits>

namespace gxr
{
// Sound interval enclosure of a real value over a box.
//
//  * `undef` marks an empty enclosure: the expression is undefined everywhere
//    on the box (e.g. sqrt of a strictly-negative interval, log of <=0). An
//    undefined enclosure contributes ZERO area and is never subdivided.
//  * `disc` marks that a discontinuity / branch cut / pole touches the box, so
//    a finite [lo,hi] cannot be trusted to be gap-free. The solver treats such
//    boxes as boundary candidates (must subdivide / split at the singularity).
//
// All arithmetic uses outward rounding (see Round.h) so AllTrue/AllFalse proofs
// are sound.
struct Interval
{
    double lo{0.0};
    double hi{0.0};
    bool undef{false};
    bool disc{false};

    constexpr Interval() = default;
    constexpr Interval(double lo_, double hi_, bool disc_ = false) : lo{lo_}, hi{hi_}, disc{disc_} {}
    constexpr explicit Interval(double v) : lo{v}, hi{v} {}

    [[nodiscard]] static constexpr Interval undefined()
    {
        Interval r;
        r.undef = true;
        r.lo = 1.0;
        r.hi = -1.0; // intentionally empty
        return r;
    }

    [[nodiscard]] static constexpr Interval whole(bool disc_ = false)
    {
        return Interval{-std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::infinity(), disc_};
    }

    [[nodiscard]] constexpr double width() const { return hi - lo; }
    [[nodiscard]] constexpr double mid() const { return lo + (hi - lo) * 0.5; }
    [[nodiscard]] constexpr bool isPoint() const { return lo == hi; }

    [[nodiscard]] constexpr bool contains(double v) const { return lo <= v && v <= hi; }
    [[nodiscard]] constexpr bool straddlesZero() const { return lo <= 0.0 && hi >= 0.0; }
    [[nodiscard]] constexpr bool allPositive() const { return lo > 0.0; }
    [[nodiscard]] constexpr bool allNegative() const { return hi < 0.0; }
    [[nodiscard]] constexpr bool allNonNegative() const { return lo >= 0.0; }
    [[nodiscard]] constexpr bool allNonPositive() const { return hi <= 0.0; }
};

// arithmetic (outward-rounded)
Interval operator+(const Interval &a, const Interval &b);
Interval operator-(const Interval &a, const Interval &b);
Interval operator*(const Interval &a, const Interval &b);
Interval operator/(const Interval &a, const Interval &b);
Interval operator-(const Interval &a); // unary negate
Interval ipow(const Interval &base, long long n);
Interval pow(const Interval &base, const Interval &exp);
Interval iabs(const Interval &a);

// transcendentals (outward-rounded, periodic extrema handled exactly)
Interval sin(const Interval &a);
Interval cos(const Interval &a);
Interval tan(const Interval &a);
Interval asin(const Interval &a);
Interval acos(const Interval &a);
Interval atan(const Interval &a);
Interval exp(const Interval &a);
Interval log(const Interval &a);
Interval sqrt(const Interval &a);

// truth-valued operators: result is [0,0]=false, [1,1]=true, [0,1]=unknown.
// `disc` propagates so a boundary touching a discontinuity is never proven.
Interval cmpLess(const Interval &a, const Interval &b);
Interval cmpLessEq(const Interval &a, const Interval &b);
Interval cmpGreater(const Interval &a, const Interval &b);
Interval cmpGreaterEq(const Interval &a, const Interval &b);
Interval cmpEqual(const Interval &a, const Interval &b);
Interval cmpNotEqual(const Interval &a, const Interval &b);
Interval logicAnd(const Interval &a, const Interval &b);
Interval logicOr(const Interval &a, const Interval &b);

[[nodiscard]] inline bool sameInterval(const Interval &a, const Interval &b)
{
    return a.lo == b.lo && a.hi == b.hi && a.undef == b.undef && a.disc == b.disc;
}
}

#endif // GXR_MATH_INTERVAL_H
