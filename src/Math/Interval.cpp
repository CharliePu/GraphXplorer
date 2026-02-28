//
// Created by charl on 6/3/2024.
//

#include "Interval.h"
#include <numbers>
#include <vector>
#include <algorithm>
#include <array>
#include <string>
#include <cmath>

bool sameInterval(const Interval &lhs, const Interval &rhs)
{
    return lhs.lower == rhs.lower && lhs.upper == rhs.upper;
}

std::ostream& operator<<(std::ostream& os, const Interval& interval) {
    os << "[" << interval.lower << ", " << interval.upper << "]";
    return os;
}

Interval operator>(const Interval& lhs, const Interval& rhs) {
    const bool candidates[] = {
        lhs.lower > rhs.lower,
        lhs.lower > rhs.upper,
        lhs.upper > rhs.lower,
        lhs.upper > rhs.upper
    };

    return Interval{
        static_cast<double>(candidates[0] && candidates[1] && candidates[2] && candidates[3]),
        static_cast<double>(candidates[0] || candidates[1] || candidates[2] || candidates[3])
    };
}

Interval operator<(const Interval& lhs, const Interval& rhs) {
    const bool candidates[] = {
        lhs.lower < rhs.lower,
        lhs.lower < rhs.upper,
        lhs.upper < rhs.lower,
        lhs.upper < rhs.upper
    };

    return Interval{
        static_cast<double>(candidates[0] && candidates[1] && candidates[2] && candidates[3]),
        static_cast<double>(candidates[0] || candidates[1] || candidates[2] || candidates[3])
    };
}

Interval operator>=(const Interval& lhs, const Interval& rhs) {
    const bool candidates[] = {
        lhs.lower >= rhs.lower,
        lhs.lower >= rhs.upper,
        lhs.upper >= rhs.lower,
        lhs.upper >= rhs.upper
    };

    return Interval{
        static_cast<double>(candidates[0] && candidates[1] && candidates[2] && candidates[3]),
        static_cast<double>(candidates[0] || candidates[1] || candidates[2] || candidates[3])
    };
}

Interval operator<=(const Interval& lhs, const Interval& rhs) {
    const bool candidates[] = {
        lhs.lower <= rhs.lower,
        lhs.lower <= rhs.upper,
        lhs.upper <= rhs.lower,
        lhs.upper <= rhs.upper
    };

    return Interval{
        static_cast<double>(candidates[0] && candidates[1] && candidates[2] && candidates[3]),
        static_cast<double>(candidates[0] || candidates[1] || candidates[2] || candidates[3])
    };
}

Interval operator&&(const Interval& lhs, const Interval& rhs) {
    const bool candidates[] = {
        lhs.lower && rhs.lower,
        lhs.lower && rhs.upper,
        lhs.upper && rhs.lower,
        lhs.upper && rhs.upper
    };

    return Interval{
        static_cast<double>(candidates[0] && candidates[1] && candidates[2] && candidates[3]),
        static_cast<double>(candidates[0] || candidates[1] || candidates[2] || candidates[3])
    };
}

Interval operator||(const Interval& lhs, const Interval& rhs) {
    const bool candidates[] = {
        lhs.lower || rhs.lower,
        lhs.lower || rhs.upper,
        lhs.upper || rhs.lower,
        lhs.upper || rhs.upper
    };

    return Interval{
        static_cast<double>(candidates[0] && candidates[1] && candidates[2] && candidates[3]),
        static_cast<double>(candidates[0] || candidates[1] || candidates[2] || candidates[3])
    };
}

Interval operator==(const Interval& lhs, const Interval& rhs) {
    if (lhs.upper < rhs.lower || rhs.upper < lhs.lower)
    {
        return {0.0, 0.0};
    }

    if (lhs.allConstant() && rhs.allConstant() && lhs.lower == rhs.lower)
    {
        return {1.0, 1.0};
    }

    return {0.0, 1.0};
}

Interval operator!=(const Interval& lhs, const Interval& rhs) {
    const auto equalRange = lhs == rhs;
    if (equalRange.allTrue())
    {
        return {0.0, 0.0};
    }

    if (equalRange.allFalse())
    {
        return {1.0, 1.0};
    }

    return {0.0, 1.0};
}

Interval operator+(const Interval& lhs, const Interval& rhs) {
    return {lhs.lower + rhs.lower, lhs.upper + rhs.upper};
}

Interval operator-(const Interval& lhs, const Interval& rhs) {
    return {lhs.lower - rhs.lower, lhs.upper - rhs.upper};
}

Interval operator*(const Interval& lhs, const Interval& rhs) {
    std::array candidates = {
        lhs.lower * rhs.lower,
        lhs.lower * rhs.upper,
        lhs.upper * rhs.lower,
        lhs.upper * rhs.upper
    };

    double min_val = std::min({ candidates[0], candidates[1], candidates[2], candidates[3] });
    double max_val = std::max({ candidates[0], candidates[1], candidates[2], candidates[3] });

    return Interval(min_val, max_val);
}

Interval operator/(const Interval& lhs, const Interval& rhs) {
    if (rhs.lower <= 0.0 && rhs.upper >= 0.0) {
        return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    }

    // Use reciprocals of the divisor interval
    std::array<double, 4> candidates = {
        lhs.lower / rhs.lower,
        lhs.lower / rhs.upper,
        lhs.upper / rhs.lower,
        lhs.upper / rhs.upper
    };

    // Compute minimum and maximum using std::min and std::max with initializer lists
    double min_val = std::min({ candidates[0], candidates[1], candidates[2], candidates[3] });
    double max_val = std::max({ candidates[0], candidates[1], candidates[2], candidates[3] });

    return Interval(min_val, max_val);
}

Interval pow(const Interval& base, const Interval& exp) {
    const auto computeIntegerPower = [](const Interval &baseInterval, const long long exponent) -> Interval
    {
        if (exponent == 0)
        {
            return {1.0, 1.0};
        }

        const auto absExponent = exponent < 0 ? -exponent : exponent;
        const auto endpointLower = std::pow(baseInterval.lower, static_cast<double>(absExponent));
        const auto endpointUpper = std::pow(baseInterval.upper, static_cast<double>(absExponent));

        Interval powered;
        if ((absExponent & 1LL) == 0LL)
        {
            const auto upper = std::max(endpointLower, endpointUpper);
            const auto lower = baseInterval.contains(0.0) ? 0.0 : std::min(endpointLower, endpointUpper);
            powered = Interval{lower, upper};
        }
        else
        {
            if (endpointLower <= endpointUpper)
            {
                powered = Interval{endpointLower, endpointUpper};
            }
            else
            {
                powered = Interval{endpointUpper, endpointLower};
            }
        }

        if (exponent > 0)
        {
            return powered;
        }

        if (powered.contains(0.0))
        {
            return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
        }

        const auto reciprocalLower = 1.0 / powered.upper;
        const auto reciprocalUpper = 1.0 / powered.lower;
        if (reciprocalLower <= reciprocalUpper)
        {
            return {reciprocalLower, reciprocalUpper};
        }

        return {reciprocalUpper, reciprocalLower};
    };

    // non-positive exp => if base is zero, undefined
    // non-zero non-integer exp => if base is negative, undefined

    if (exp.allIntConst())
    {
        return computeIntegerPower(base, static_cast<long long>(exp.lower));
    }

    if (!exp.allPositive() && base.anyZero())
        return INTERVAL_UNDEFINED;

    if (!exp.allZero() && !exp.allIntConst() && base.anyNegative())
        return INTERVAL_UNDEFINED;


    std::vector<double> results{
        std::pow(base.lower, exp.lower),
        std::pow(base.lower, exp.upper),
        std::pow(base.upper, exp.lower),
        std::pow(base.upper, exp.upper)
    };

    double min_val = *std::ranges::min_element(results);
    double max_val = *std::ranges::max_element(results);
    return Interval(min_val, max_val);
}


double Interval::size() const {
    return upper - lower;
}

bool Interval::contains(const Interval& interval) const {
    return lower <= interval.lower && interval.upper <= upper;
}

bool Interval::strictlyContains(const Interval& interval) const {
    return lower < interval.lower && interval.upper < upper;
}

bool Interval::anyZero() const {
    return lower < 0 && upper > 0;
}

bool Interval::anyPositive() const
{
    return upper > 0;
}

bool Interval::anyNegative() const
{
    return lower < 0;
}

double Interval::mid() const {
    return lower + (upper - lower) / 2;
}

bool Interval::allConstant() const
{
    return lower == upper;
}

bool Interval::allIntConst() const
{
    return allConstant() && std::floor(lower) == lower;
}

// Computes the minimum and maximum of a function over an interval
typedef double (*Func)(double);

Interval computeInterval(double lower, double upper, Func func, const std::vector<double>& critical_points = {}) {
    std::vector<double> points = {lower, upper};
    for (double cp : critical_points) {
        if (cp > lower && cp < upper) {
            points.emplace_back(cp);
        }
    }

    // Remove duplicates and sort
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());

    // Evaluate function at all points
    double min_val = func(points[0]);
    double max_val = func(points[0]);
    for (double x : points) {
        double fx = func(x);
        if (fx < min_val) min_val = fx;
        if (fx > max_val) max_val = fx;
    }

    return Interval(min_val, max_val);
}

Interval sin(const Interval& interval) {
    constexpr auto pi = std::numbers::pi;
    // Critical points where derivative is zero: multiples of pi/2
    std::vector<double> criticals;
    // Calculate k such that (pi/2) + k*pi is within [lower, upper]
    double k_min = std::ceil((interval.lower - pi / 2) / pi);
    double k_max = std::floor((interval.upper - pi / 2) / pi);
    for (double k = k_min; k <= k_max; ++k) {
        double cp = pi / 2 + k * pi;
        if (cp > interval.lower && cp < interval.upper) {
            criticals.emplace_back(cp);
        }
    }

    return computeInterval(interval.lower, interval.upper, static_cast<Func>(std::sin), criticals);
}

Interval cos(const Interval& interval) {
    constexpr auto pi = std::numbers::pi;
    // Critical points where derivative is zero: multiples of pi
    std::vector<double> criticals;
    // Calculate k such that k*pi is within [lower, upper]
    double k_min = std::ceil(interval.lower / pi);
    double k_max = std::floor(interval.upper / pi);
    for (double k = k_min; k <= k_max; ++k) {
        double cp = k * pi;
        if (cp > interval.lower && cp < interval.upper) {
            criticals.emplace_back(cp);
        }
    }

    return computeInterval(interval.lower, interval.upper, static_cast<Func>(std::cos), criticals);
}

Interval tan(const Interval& interval) {
    constexpr auto pi = std::numbers::pi;
    // Check for discontinuities at (pi/2) + k*pi
    double k_min = std::ceil((interval.lower - pi / 2) / pi);
    double k_max = std::floor((interval.upper - pi / 2) / pi);
    for (double k = k_min; k <= k_max; ++k) {
        double asymptote = pi / 2 + k * pi;
        if (asymptote > interval.lower && asymptote < interval.upper) {
            // Interval crosses a discontinuity; tan approaches -inf and +inf
            return Interval(-std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::infinity());
        }
    }

    // tan is strictly increasing where defined, so min and max are at endpoints
    double tan_lower = std::tan(interval.lower);
    double tan_upper = std::tan(interval.upper);
    if (tan_lower < tan_upper) {
        return Interval(tan_lower, tan_upper);
    } else {
        return Interval(tan_upper, tan_lower);
    }
}

Interval log(const Interval& interval) {
    if (interval.lower <= 0.0) {
        throw std::domain_error("Log undefined for non-positive values.");
    }

    // Log is strictly increasing
    double log_lower = std::log(interval.lower);
    double log_upper = std::log(interval.upper);
    return Interval(log_lower, log_upper);
}

Interval exp(const Interval& interval) {
    // Exp is strictly increasing
    double exp_lower = std::exp(interval.lower);
    double exp_upper = std::exp(interval.upper);
    return Interval(exp_lower, exp_upper);
}

Interval sqrt(const Interval& interval) {
    if (interval.lower < 0.0) {
        throw std::domain_error("Sqrt undefined for negative values.");
    }

    // Sqrt is strictly increasing
    double sqrt_lower = std::sqrt(interval.lower);
    double sqrt_upper = std::sqrt(interval.upper);
    return Interval(sqrt_lower, sqrt_upper);
}
