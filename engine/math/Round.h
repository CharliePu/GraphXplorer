#ifndef GXR_MATH_ROUND_H
#define GXR_MATH_ROUND_H

#include <cmath>
#include <limits>

// Directed-rounding helpers for sound interval arithmetic.
//
// Rather than switching the FPU rounding mode per operation (fragile under
// MSVC /fp:precise and across threads), we compute each elementary result in
// round-to-nearest and then widen outward by one ULP. A single IEEE operation
// has <= 0.5 ULP error, so nextafter-widening the *result* of each op is a
// sound outward enclosure. Compound expressions stay sound because every op
// widens. The pessimism is ~1 ULP/op which is far below pixel scale.

namespace gxr
{
inline constexpr double kInf = std::numeric_limits<double>::infinity();

// round toward -inf (widen a lower bound)
[[nodiscard]] inline double rdown(const double x) noexcept
{
    if (!std::isfinite(x))
    {
        return x;
    }
    return std::nextafter(x, -kInf);
}

// round toward +inf (widen an upper bound)
[[nodiscard]] inline double rup(const double x) noexcept
{
    if (!std::isfinite(x))
    {
        return x;
    }
    return std::nextafter(x, kInf);
}

// Widen by n ULPs in each direction (for transcendentals whose std::
// implementations may carry a couple ULP of error).
[[nodiscard]] inline double rdownN(double x, int n) noexcept
{
    while (n-- > 0)
    {
        x = rdown(x);
    }
    return x;
}

[[nodiscard]] inline double rupN(double x, int n) noexcept
{
    while (n-- > 0)
    {
        x = rup(x);
    }
    return x;
}
}

#endif // GXR_MATH_ROUND_H
