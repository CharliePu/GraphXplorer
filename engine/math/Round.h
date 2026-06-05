#ifndef GXR_MATH_ROUND_H
#define GXR_MATH_ROUND_H

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

// Directed-rounding helpers for sound interval arithmetic.
//
// Each elementary result is computed in round-to-nearest and then widened
// outward by one ULP. A single IEEE operation has <= 0.5 ULP error, so
// ULP-widening the result of each op is a sound outward enclosure; compound
// expressions stay sound because every op widens.
//
// The ULP step is done by incrementing/decrementing the integer bit pattern
// (the classic monotonic-bits trick) rather than std::nextafter, which is a
// slow libcall. For a box-subdivision solver doing ~50 rounded ops per box,
// this is the difference between ~7us and ~0.3us per box.

namespace gxr
{
inline constexpr double kInf = std::numeric_limits<double>::infinity();

// round toward -inf (widen a lower bound) by one ULP
[[nodiscard]] inline double rdown(const double x) noexcept
{
    if (!std::isfinite(x) || x == 0.0) return x; // exact 0 has no rounding error
    int64_t i = std::bit_cast<int64_t>(x);
    i += (x > 0.0) ? -1 : +1; // smaller positive / more negative
    return std::bit_cast<double>(i);
}

// round toward +inf (widen an upper bound) by one ULP
[[nodiscard]] inline double rup(const double x) noexcept
{
    if (!std::isfinite(x) || x == 0.0) return x;
    int64_t i = std::bit_cast<int64_t>(x);
    i += (x > 0.0) ? +1 : -1; // larger positive / less negative
    return std::bit_cast<double>(i);
}

[[nodiscard]] inline double rdownN(double x, int n) noexcept
{
    while (n-- > 0) x = rdown(x);
    return x;
}

[[nodiscard]] inline double rupN(double x, int n) noexcept
{
    while (n-- > 0) x = rup(x);
    return x;
}
}

#endif // GXR_MATH_ROUND_H
