#include "Interval.h"

#include "Round.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace gxr
{
namespace
{
constexpr double kPi = std::numbers::pi;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

// When the phase of a trig argument cannot be located to better than this many
// radians (because the argument magnitude is so large that mod-2pi reduction and
// critical-point location both lose accuracy), the value is treated as fully
// unresolved: [-1,1]. This keeps sin/cos/tan SOUND for huge arguments such as
// 2^x at large x, where std-lib range reduction is unreliable.
constexpr double kPhaseGuard = 0.25;

bool anyUndef(const Interval &a, const Interval &b) { return a.undef || b.undef; }

// NaN/encoding guard: a finite-math op that produced NaN (e.g. inf*0) is
// collapsed to a discontinuous whole-line enclosure rather than poisoning the
// pipeline with NaN.
Interval sanitize(double lo, double hi, bool disc)
{
    if (std::isnan(lo) || std::isnan(hi))
    {
        return Interval::whole(true);
    }
    if (lo > hi)
    {
        std::swap(lo, hi);
    }
    return Interval{lo, hi, disc};
}

double ulpMag(const Interval &a)
{
    const double m = std::max(std::abs(a.lo), std::abs(a.hi));
    if (!std::isfinite(m))
    {
        return kInf;
    }
    // magnitude of one ULP near m; covers std-lib range-reduction error for
    // large arguments to the trig functions.
    return std::nextafter(m, kInf) - m;
}

// Truth helpers: [0,0] false, [1,1] true, [0,1] unknown.
Interval truthFalse(bool disc) { return Interval{0.0, 0.0, disc}; }
Interval truthTrue(bool disc) { return Interval{1.0, 1.0, disc}; }
Interval truthUnknown(bool disc) { return Interval{0.0, 1.0, disc}; }

// Does an integer-shifted critical point  phase + k*period  fall strictly
// inside (lo, hi)?
bool hasCriticalInside(double lo, double hi, double phase, double period)
{
    const double k = std::ceil((lo - phase) / period);
    const double p = phase + k * period;
    return p > lo && p < hi;
}
}

Interval operator+(const Interval &a, const Interval &b)
{
    if (anyUndef(a, b)) return Interval::undefined();
    return sanitize(rdown(a.lo + b.lo), rup(a.hi + b.hi), a.disc || b.disc);
}

Interval operator-(const Interval &a, const Interval &b)
{
    if (anyUndef(a, b)) return Interval::undefined();
    return sanitize(rdown(a.lo - b.hi), rup(a.hi - b.lo), a.disc || b.disc);
}

Interval operator-(const Interval &a)
{
    if (a.undef) return a;
    return Interval{-a.hi, -a.lo, a.disc};
}

Interval operator*(const Interval &a, const Interval &b)
{
    if (anyUndef(a, b)) return Interval::undefined();
    const std::array<double, 4> p{a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
    double lo = std::min({p[0], p[1], p[2], p[3]});
    double hi = std::max({p[0], p[1], p[2], p[3]});
    return sanitize(rdown(lo), rup(hi), a.disc || b.disc);
}

Interval operator/(const Interval &a, const Interval &b)
{
    if (anyUndef(a, b)) return Interval::undefined();
    if (b.lo <= 0.0 && b.hi >= 0.0)
    {
        if (b.lo == 0.0 && b.hi == 0.0)
        {
            return Interval::undefined();
        }
        // divisor straddles 0 -> pole inside the box.
        return Interval::whole(true);
    }
    const std::array<double, 4> p{a.lo / b.lo, a.lo / b.hi, a.hi / b.lo, a.hi / b.hi};
    double lo = std::min({p[0], p[1], p[2], p[3]});
    double hi = std::max({p[0], p[1], p[2], p[3]});
    return sanitize(rdown(lo), rup(hi), a.disc || b.disc);
}

Interval iabs(const Interval &a)
{
    if (a.undef) return a;
    if (a.lo >= 0.0) return a;
    if (a.hi <= 0.0) return Interval{-a.hi, -a.lo, a.disc};
    return Interval{0.0, std::max(-a.lo, a.hi), a.disc};
}

Interval ipow(const Interval &base, long long n)
{
    if (base.undef) return base;
    if (n == 0) return Interval{1.0, 1.0, base.disc};

    const long long k = n < 0 ? -n : n;
    auto powMag = [](double x, long long e) {
        double r = 1.0, f = std::abs(x);
        while (e > 0)
        {
            if (e & 1) r *= f;
            e >>= 1;
            if (e > 0) f *= f;
        }
        return r;
    };

    Interval pos; // base^k for k>0
    const double la = powMag(base.lo, k);
    const double ha = powMag(base.hi, k);
    if ((k & 1) == 0)
    {
        const double hi = std::max(la, ha);
        if (base.contains(0.0))
        {
            // exact mathematical floor of an even power is 0; do not widen it
            // below zero (that would only manufacture a false negative).
            pos = sanitize(0.0, rup(hi), base.disc);
        }
        else
        {
            pos = sanitize(rdown(std::min(la, ha)), rup(hi), base.disc);
        }
    }
    else
    {
        const double el = std::copysign(la, base.lo);
        const double eh = std::copysign(ha, base.hi);
        pos = sanitize(rdown(std::min(el, eh)), rup(std::max(el, eh)), base.disc);
    }

    if (n > 0) return pos;
    // negative exponent -> reciprocal
    if (pos.lo <= 0.0 && pos.hi >= 0.0) return Interval::whole(true);
    const double rl = 1.0 / pos.hi, rh = 1.0 / pos.lo;
    return sanitize(rdown(std::min(rl, rh)), rup(std::max(rl, rh)), base.disc);
}

Interval pow(const Interval &base, const Interval &exp)
{
    if (anyUndef(base, exp)) return Interval::undefined();
    if (exp.isPoint() && std::floor(exp.lo) == exp.lo && std::abs(exp.lo) < 1e9)
    {
        return ipow(base, static_cast<long long>(exp.lo));
    }
    // general real exponent: requires positive base (else undefined / branch).
    if (base.hi < 0.0) return Interval::undefined();
    Interval b = base;
    bool disc = base.disc || exp.disc;
    if (b.lo <= 0.0)
    {
        b.lo = 0.0;
        disc = true; // boundary at base=0
    }
    const std::array<double, 4> p{std::pow(b.lo, exp.lo), std::pow(b.lo, exp.hi),
                                  std::pow(b.hi, exp.lo), std::pow(b.hi, exp.hi)};
    double lo = std::min({p[0], p[1], p[2], p[3]});
    double hi = std::max({p[0], p[1], p[2], p[3]});
    return sanitize(rdownN(lo, 2), rupN(hi, 2), disc);
}

Interval asin(const Interval &a)
{
    if (a.undef) return a;
    if (a.lo > 1.0 || a.hi < -1.0) return Interval::undefined();
    const bool disc = a.disc || a.lo < -1.0 || a.hi > 1.0; // domain edge crossed
    const double lo = std::asin(std::clamp(a.lo, -1.0, 1.0));
    const double hi = std::asin(std::clamp(a.hi, -1.0, 1.0));
    return sanitize(rdownN(lo, 2), rupN(hi, 2), disc); // increasing
}

Interval acos(const Interval &a)
{
    if (a.undef) return a;
    if (a.lo > 1.0 || a.hi < -1.0) return Interval::undefined();
    const bool disc = a.disc || a.lo < -1.0 || a.hi > 1.0;
    const double lo = std::acos(std::clamp(a.hi, -1.0, 1.0)); // decreasing
    const double hi = std::acos(std::clamp(a.lo, -1.0, 1.0));
    return sanitize(rdownN(lo, 2), rupN(hi, 2), disc);
}

Interval atan(const Interval &a)
{
    if (a.undef) return a;
    return sanitize(rdownN(std::atan(a.lo), 2), rupN(std::atan(a.hi), 2), a.disc); // increasing
}

Interval exp(const Interval &a)
{
    if (a.undef) return a;
    return sanitize(rdownN(std::exp(a.lo), 2), rupN(std::exp(a.hi), 2), a.disc);
}

Interval log(const Interval &a)
{
    if (a.undef) return a;
    if (a.hi <= 0.0) return Interval::undefined();
    if (a.lo <= 0.0)
    {
        return Interval{-kInf, rupN(std::log(a.hi), 2), true};
    }
    return sanitize(rdownN(std::log(a.lo), 2), rupN(std::log(a.hi), 2), a.disc);
}

Interval sqrt(const Interval &a)
{
    if (a.undef) return a;
    if (a.hi < 0.0) return Interval::undefined();
    const double lo = a.lo <= 0.0 ? 0.0 : rdownN(std::sqrt(a.lo), 1);
    const bool disc = a.disc || a.lo < 0.0;
    return sanitize(lo, rupN(std::sqrt(a.hi), 1), disc);
}

Interval sin(const Interval &a)
{
    if (a.undef) return a;
    const double u = ulpMag(a);
    const double lo = a.lo - u, hi = a.hi + u;
    if (!std::isfinite(lo) || !std::isfinite(hi) || (hi - lo) >= kTwoPi || u >= kPhaseGuard)
    {
        return Interval{-1.0, 1.0, a.disc};
    }
    double rl = std::sin(lo), rh = std::sin(hi);
    if (rl > rh) std::swap(rl, rh);
    if (hasCriticalInside(lo, hi, kPi / 2.0, kTwoPi)) rh = 1.0;
    if (hasCriticalInside(lo, hi, -kPi / 2.0, kTwoPi)) rl = -1.0;
    rl = std::max(-1.0, rdownN(rl, 2));
    rh = std::min(1.0, rupN(rh, 2));
    return Interval{rl, rh, a.disc};
}

Interval cos(const Interval &a)
{
    if (a.undef) return a;
    const double u = ulpMag(a);
    const double lo = a.lo - u, hi = a.hi + u;
    if (!std::isfinite(lo) || !std::isfinite(hi) || (hi - lo) >= kTwoPi || u >= kPhaseGuard)
    {
        return Interval{-1.0, 1.0, a.disc};
    }
    double rl = std::cos(lo), rh = std::cos(hi);
    if (rl > rh) std::swap(rl, rh);
    if (hasCriticalInside(lo, hi, 0.0, kTwoPi)) rh = 1.0;
    if (hasCriticalInside(lo, hi, kPi, kTwoPi)) rl = -1.0;
    rl = std::max(-1.0, rdownN(rl, 2));
    rh = std::min(1.0, rupN(rh, 2));
    return Interval{rl, rh, a.disc};
}

Interval tan(const Interval &a)
{
    if (a.undef) return a;
    const double u = ulpMag(a);
    const double lo = a.lo - u, hi = a.hi + u;
    if (!std::isfinite(lo) || !std::isfinite(hi) || (hi - lo) >= kPi || u >= kPhaseGuard
        || hasCriticalInside(lo, hi, kPi / 2.0, kPi))
    {
        return Interval::whole(true); // pole inside / unresolved phase
    }
    double rl = std::tan(lo), rh = std::tan(hi);
    if (rl > rh) std::swap(rl, rh);
    return sanitize(rdownN(rl, 2), rupN(rh, 2), a.disc);
}

// ---- truth-valued comparison / logic ----

Interval cmpLess(const Interval &a, const Interval &b)
{
    if (anyUndef(a, b)) return Interval::undefined();
    const bool disc = a.disc || b.disc;
    if (a.hi < b.lo) return truthTrue(disc);
    if (a.lo >= b.hi) return truthFalse(disc);
    return truthUnknown(disc);
}

Interval cmpLessEq(const Interval &a, const Interval &b)
{
    if (anyUndef(a, b)) return Interval::undefined();
    const bool disc = a.disc || b.disc;
    if (a.hi <= b.lo) return truthTrue(disc);
    if (a.lo > b.hi) return truthFalse(disc);
    return truthUnknown(disc);
}

Interval cmpGreater(const Interval &a, const Interval &b) { return cmpLess(b, a); }
Interval cmpGreaterEq(const Interval &a, const Interval &b) { return cmpLessEq(b, a); }

Interval cmpEqual(const Interval &a, const Interval &b)
{
    if (anyUndef(a, b)) return Interval::undefined();
    const bool disc = a.disc || b.disc;
    if (a.hi < b.lo || b.hi < a.lo) return truthFalse(disc);
    if (a.isPoint() && b.isPoint() && a.lo == b.lo) return truthTrue(disc);
    return truthUnknown(disc);
}

Interval cmpNotEqual(const Interval &a, const Interval &b)
{
    const Interval eq = cmpEqual(a, b);
    if (eq.undef) return eq;
    if (eq.lo == 1.0 && eq.hi == 1.0) return truthFalse(eq.disc);
    if (eq.lo == 0.0 && eq.hi == 0.0) return truthTrue(eq.disc);
    return truthUnknown(eq.disc);
}

Interval logicAnd(const Interval &a, const Interval &b)
{
    // operands are truth intervals (possibly undefined on part of the box).
    const bool disc = a.disc || b.disc;
    const bool aF = (a.undef) || (a.hi <= 0.0);
    const bool bF = (b.undef) || (b.hi <= 0.0);
    if (aF || bF) return truthFalse(disc);
    const bool aT = !a.undef && a.lo >= 1.0;
    const bool bT = !b.undef && b.lo >= 1.0;
    if (aT && bT) return truthTrue(disc);
    return truthUnknown(disc || a.undef || b.undef);
}

Interval logicOr(const Interval &a, const Interval &b)
{
    const bool disc = a.disc || b.disc;
    const bool aT = !a.undef && a.lo >= 1.0;
    const bool bT = !b.undef && b.lo >= 1.0;
    if (aT || bT) return truthTrue(disc);
    const bool aF = a.undef || a.hi <= 0.0;
    const bool bF = b.undef || b.hi <= 0.0;
    if (aF && bF) return truthFalse(disc);
    return truthUnknown(disc);
}
}
