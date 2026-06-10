#ifndef GXR_MATH_SIMD_H
#define GXR_MATH_SIMD_H

// A tiny fixed-width SIMD value type in PLAIN C++ -- no intrinsics anywhere.
// The elementwise loops are written so the compiler auto-vectorizes them (the
// engine library builds with /arch:AVX2 on MSVC x64, giving 4-wide double FMA;
// without that flag everything still compiles and runs, just narrower).
//
// The shape deliberately mirrors std::simd<double>: when MSVC's STL ships the
// C++26 <simd> header, `vd` becomes an alias and the kernels keep their form.
// (Probed 2026-06-10: this MSVC has neither <simd> nor <experimental/simd>.)
//
// The transcendental kernels (vexp2 / vexp / vsincos) are used ONLY by the
// solver's point-SAMPLING paths -- measure estimation of already-uncertain
// cells -- never by the certified interval arithmetic, which keeps its
// directed-rounding scalar implementation. Coefficients are exact Taylor
// reciprocal factorials (computed, not transcribed), so there are no opaque
// minimax tables to trust; accuracy is a few ulp, far beyond the sampling
// tolerance, and the results are deterministic across runs by construction.

#include <bit>
#include <cmath>
#include <cstdint>

namespace gxr
{
inline constexpr int kLanes = 8; // two AVX2 registers per op: good autovec width

struct vd
{
    double v[kLanes];
};

// FMA: with AVX2 codegen std::fma is a single instruction; without it MSVC
// calls a slow software fma, so fall back to mul+add (per-build deterministic).
// The transcendental fast paths REQUIRE real fma (their argument reductions
// rely on the fused product's single rounding); without it they defer to the
// CRT per lane -- correct everywhere, fast where the ISA allows.
#if defined(__AVX2__) || defined(__FMA__)
#define GXR_SIMD_FMA 1
inline double vfma1(double a, double b, double c) { return std::fma(a, b, c); }
#else
#define GXR_SIMD_FMA 0
inline double vfma1(double a, double b, double c) { return a * b + c; }
#endif

inline vd vsplat(double x)
{
    vd r;
    for (int i = 0; i < kLanes; ++i) r.v[i] = x;
    return r;
}
inline vd vadd(const vd &a, const vd &b)
{
    vd r;
    for (int i = 0; i < kLanes; ++i) r.v[i] = a.v[i] + b.v[i];
    return r;
}
inline vd vsub(const vd &a, const vd &b)
{
    vd r;
    for (int i = 0; i < kLanes; ++i) r.v[i] = a.v[i] - b.v[i];
    return r;
}
inline vd vmul(const vd &a, const vd &b)
{
    vd r;
    for (int i = 0; i < kLanes; ++i) r.v[i] = a.v[i] * b.v[i];
    return r;
}
inline vd vfma(const vd &a, const vd &b, const vd &c)
{
    vd r;
    for (int i = 0; i < kLanes; ++i) r.v[i] = vfma1(a.v[i], b.v[i], c.v[i]);
    return r;
}

// ---- exp2 / exp ------------------------------------------------------------
// 2^x = 2^k * e^(r ln2), k = round(x), r = x-k in [-0.5,0.5]. The scale 2^k is
// applied as TWO exponent-built factors so the gradual-underflow and the full
// normal range are covered branchlessly (k in [-2044, 2046]).

namespace simd_detail
{
// e^y for |y| <= 0.3466 (= 0.5*ln2): Taylor to y^14; the y^15/15! tail is
// ~9.5e-20 relative -- below half an ulp.
inline double expPoly(double y)
{
    constexpr double c14 = 1.0 / 87178291200.0;
    constexpr double c13 = 1.0 / 6227020800.0;
    constexpr double c12 = 1.0 / 479001600.0;
    constexpr double c11 = 1.0 / 39916800.0;
    constexpr double c10 = 1.0 / 3628800.0;
    constexpr double c9 = 1.0 / 362880.0;
    constexpr double c8 = 1.0 / 40320.0;
    constexpr double c7 = 1.0 / 5040.0;
    constexpr double c6 = 1.0 / 720.0;
    constexpr double c5 = 1.0 / 120.0;
    constexpr double c4 = 1.0 / 24.0;
    constexpr double c3 = 1.0 / 6.0;
    constexpr double c2 = 1.0 / 2.0;
    double p = c14;
    p = vfma1(p, y, c13);
    p = vfma1(p, y, c12);
    p = vfma1(p, y, c11);
    p = vfma1(p, y, c10);
    p = vfma1(p, y, c9);
    p = vfma1(p, y, c8);
    p = vfma1(p, y, c7);
    p = vfma1(p, y, c6);
    p = vfma1(p, y, c5);
    p = vfma1(p, y, c4);
    p = vfma1(p, y, c3);
    p = vfma1(p, y, c2);
    p = vfma1(p, y, 1.0);
    p = vfma1(p, y, 1.0);
    return p;
}

inline double scaleByPow2(double m, long long k)
{
    // 2^k as two factors so k in [-2044, 2046] (incl. subnormal results) works.
    const long long k1 = k / 2, k2 = k - k1;
    const double s1 = std::bit_cast<double>(static_cast<uint64_t>(k1 + 1023) << 52);
    const double s2 = std::bit_cast<double>(static_cast<uint64_t>(k2 + 1023) << 52);
    return (m * s1) * s2;
}
} // namespace simd_detail

// ln2 split for exact r = x*ln2 - k*ln2 style reductions (fma-based). The tail
// is DERIVED from the fdlibm hi/lo pair (ln2 to ~70 bits) rather than
// transcribed: (hi - kLn2) is exact (the values are close), + lo costs one
// rounding, so kLn2 + kLn2Tail == ln2 to ~1e-26.
inline constexpr double kLn2 = 0.6931471805599453; // ln2 rounded to double
inline constexpr double kLn2FdlibmHi = 6.93147180369123816490e-01;
inline constexpr double kLn2FdlibmLo = 1.90821492927058770002e-10;
inline constexpr double kLn2Tail = (kLn2FdlibmHi - kLn2) + kLn2FdlibmLo;

// Columnar, BRANCHLESS main path so the lane loops auto-vectorize (any branch
// or early-exit defeats the vectorizer). Out-of-range and non-finite inputs
// are clamped through the arithmetic harmlessly, detected with one cheap mask
// reduction, and patched by a scalar fixup loop only when actually present.
inline vd vexp2(const vd &x)
{
    vd r;
#if !GXR_SIMD_FMA
    for (int i = 0; i < kLanes; ++i) r.v[i] = std::exp2(x.v[i]);
    return r;
#else
    double kd[kLanes], y[kLanes], p[kLanes];
    int special = 0;
    for (int i = 0; i < kLanes; ++i)
    {
        const double xi = x.v[i];
        // clamp keeps the arithmetic finite for wild inputs; real value fixed up below
        const double xc = xi < -1080.0 ? -1080.0 : (xi > 1030.0 ? 1030.0 : xi);
        const double k = std::floor(xc + 0.5);
        kd[i] = k;
        const double rr = xc - k;
        y[i] = vfma1(rr, kLn2Tail, rr * kLn2);
        special += !(xi > -1075.0 && xi < 1025.0); // NaN or out of range
    }
    for (int i = 0; i < kLanes; ++i) // pure fma chain: vectorizes 4-wide
        p[i] = simd_detail::expPoly(y[i]);
    for (int i = 0; i < kLanes; ++i)
        r.v[i] = simd_detail::scaleByPow2(p[i], static_cast<long long>(kd[i]));
    if (special)
        for (int i = 0; i < kLanes; ++i)
        {
            const double xi = x.v[i];
            if (!(xi == xi)) r.v[i] = xi;
            else if (xi >= 1025.0) r.v[i] = HUGE_VAL;
            else if (xi <= -1075.0) r.v[i] = 0.0;
        }
    return r;
#endif
}

inline vd vexp(const vd &x)
{
    vd r;
#if !GXR_SIMD_FMA
    for (int i = 0; i < kLanes; ++i) r.v[i] = std::exp(x.v[i]);
    return r;
#else
    constexpr double kInvLn2 = 1.4426950408889634; // 1/ln2
    double kd[kLanes], y[kLanes], p[kLanes];
    int special = 0;
    for (int i = 0; i < kLanes; ++i)
    {
        const double xi = x.v[i];
        const double xc = xi < -750.0 ? -750.0 : (xi > 715.0 ? 715.0 : xi);
        const double k = std::floor(vfma1(xc, kInvLn2, 0.5));
        kd[i] = k;
        double rr = vfma1(-k, kLn2, xc);
        rr = vfma1(-k, kLn2Tail, rr);
        y[i] = rr;
        special += !(xi > -745.0 && xi < 710.0);
    }
    for (int i = 0; i < kLanes; ++i) p[i] = simd_detail::expPoly(y[i]);
    for (int i = 0; i < kLanes; ++i)
        r.v[i] = simd_detail::scaleByPow2(p[i], static_cast<long long>(kd[i]));
    if (special)
        for (int i = 0; i < kLanes; ++i)
        {
            const double xi = x.v[i];
            if (!(xi == xi)) r.v[i] = xi;
            else if (xi >= 710.0) r.v[i] = HUGE_VAL;
            else if (xi <= -745.0) r.v[i] = 0.0;
        }
    return r;
#endif
}

// ---- sin / cos --------------------------------------------------------------
// FMA-based two-step Cody-Waite: k = round(x * 2/pi), r = fma(-k, pi/2, x)
// then fma(-k, pi/2_tail, r). With fused multiplies this stays accurate to
// ~1e-20 in r for |x| up to ~2^45; beyond that the caller's lanes fall back to
// std::sin/std::cos (the CRT does full Payne-Hanek reduction).

inline constexpr double kPio2 = 1.5707963267948966;        // pi/2 in double
inline constexpr double kPio2Tail = 6.123233995736766e-17; // pi/2 - kPio2
inline constexpr double kTwoOverPi = 0.6366197723675814;
inline constexpr double kSinCosHugeArg = 3.5184372088832e13; // 2^45

namespace simd_detail
{
// sin(r)/r-style Taylor on |r| <= pi/4 + slack: terms to r^19 (tail ~2e-20).
inline double sinPoly(double r)
{
    const double z = r * r;
    constexpr double s9 = -1.0 / 121645100408832000.0; // -1/19!
    constexpr double s8 = 1.0 / 355687428096000.0;     //  1/17!
    constexpr double s7 = -1.0 / 1307674368000.0;      // -1/15!
    constexpr double s6 = 1.0 / 6227020800.0;          //  1/13!
    constexpr double s5 = -1.0 / 39916800.0;           // -1/11!
    constexpr double s4 = 1.0 / 362880.0;              //  1/9!
    constexpr double s3 = -1.0 / 5040.0;               // -1/7!
    constexpr double s2 = 1.0 / 120.0;                 //  1/5!
    constexpr double s1 = -1.0 / 6.0;                  // -1/3!
    double p = s9;
    p = vfma1(p, z, s8);
    p = vfma1(p, z, s7);
    p = vfma1(p, z, s6);
    p = vfma1(p, z, s5);
    p = vfma1(p, z, s4);
    p = vfma1(p, z, s3);
    p = vfma1(p, z, s2);
    p = vfma1(p, z, s1);
    return vfma1(p * z, r, r); // r + r*z*P(z)
}

inline double cosPoly(double r)
{
    const double z = r * r;
    constexpr double c9 = 1.0 / 6402373705728000.0; //  1/18!
    constexpr double c8 = -1.0 / 20922789888000.0;  // -1/16!
    constexpr double c7 = 1.0 / 87178291200.0;      //  1/14!
    constexpr double c6 = -1.0 / 479001600.0;       // -1/12!
    constexpr double c5 = 1.0 / 3628800.0;          //  1/10!
    constexpr double c4 = -1.0 / 40320.0;           // -1/8!
    constexpr double c3 = 1.0 / 720.0;              //  1/6!
    constexpr double c2 = -1.0 / 24.0;              //  1/4!
    constexpr double c1 = 1.0 / 2.0;                //  1/2!
    double p = c9;
    p = vfma1(p, z, c8);
    p = vfma1(p, z, c7);
    p = vfma1(p, z, c6);
    p = vfma1(p, z, c5);
    p = vfma1(p, z, c4);
    p = vfma1(p, z, c3);
    p = vfma1(p, z, c2);
    p = vfma1(p, z, c1);
    return vfma1(-p, z, 1.0); // 1 - z*Q(z)
}
} // namespace simd_detail

// Computes sin and cos together (shared reduction). Lanes with |x| >= 2^45 or
// non-finite x use the CRT (correct for any argument, just slower; such lanes
// are rare -- e.g. sampling sin(2^x) only meets them past x ~ 45). The main
// path is columnar and branchless -- the quadrant selection is arithmetic
// (mod-4 via floors, sign via 1-2m), so every loop auto-vectorizes.
inline void vsincos(const vd &x, vd &s, vd &c)
{
#if !GXR_SIMD_FMA
    for (int i = 0; i < kLanes; ++i)
    {
        s.v[i] = std::sin(x.v[i]);
        c.v[i] = std::cos(x.v[i]);
    }
    return;
#else
    double sp[kLanes], cp[kLanes], qd[kLanes];
    int huge = 0;
    for (int i = 0; i < kLanes; ++i)
    {
        const double xi = x.v[i];
        const double axi = xi < 0.0 ? -xi : xi;
        huge += !(axi < kSinCosHugeArg);
        const double xc = axi < kSinCosHugeArg ? xi : 0.0; // keep arithmetic finite
        const double kd = std::floor(vfma1(xc, kTwoOverPi, 0.5));
        double r = vfma1(-kd, kPio2, xc);
        r = vfma1(-kd, kPio2Tail, r);
        // q = kd mod 4 as a double in {0,1,2,3} (floor-based, branchless; kd
        // is exactly representable so the floors are exact)
        qd[i] = kd - 4.0 * std::floor(kd * 0.25);
        sp[i] = r; // stash r; polys run in their own vectorized passes
        cp[i] = r;
    }
    for (int i = 0; i < kLanes; ++i) sp[i] = simd_detail::sinPoly(sp[i]);
    for (int i = 0; i < kLanes; ++i) cp[i] = simd_detail::cosPoly(cp[i]);
    for (int i = 0; i < kLanes; ++i)
    {
        const double q = qd[i];
        const double odd = q - 2.0 * std::floor(q * 0.5); // q mod 2: swap sin/cos
        const double sBase = sp[i] + (cp[i] - sp[i]) * odd;
        const double cBase = cp[i] + (sp[i] - cp[i]) * odd;
        const double sNeg = std::floor(q * 0.5);              // q in {2,3}: negate sin
        const double qc = q + 1.0 - 4.0 * std::floor((q + 1.0) * 0.25);
        const double cNeg = std::floor(qc * 0.5);             // q in {1,2}: negate cos
        s.v[i] = sBase * (1.0 - 2.0 * sNeg);
        c.v[i] = cBase * (1.0 - 2.0 * cNeg);
    }
    if (huge)
        for (int i = 0; i < kLanes; ++i)
        {
            const double xi = x.v[i];
            const double axi = xi < 0.0 ? -xi : xi;
            if (!(axi < kSinCosHugeArg))
            {
                s.v[i] = std::sin(xi);
                c.v[i] = std::cos(xi);
            }
        }
#endif
}
}

#endif // GXR_MATH_SIMD_H
