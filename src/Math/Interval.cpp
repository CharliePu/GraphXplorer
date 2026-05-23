//
// Created by charl on 6/3/2024.
//

#include "Interval.h"
#include <numbers>
#include <algorithm>
#include <array>
#include <cmath>

namespace
{
bool carriesUnresolvedDomain(const Interval &interval)
{
    return interval.hasDiscontinuity() || interval.undefined();
}

bool carriesUnresolvedDomain(const Interval &lhs, const Interval &rhs)
{
    return carriesUnresolvedDomain(lhs) || carriesUnresolvedDomain(rhs);
}

Interval undefinedIntervalFrom(const Interval &interval)
{
    return {1.0, 0.0, carriesUnresolvedDomain(interval)};
}

Interval undefinedIntervalFrom(const Interval &lhs, const Interval &rhs)
{
    return {1.0, 0.0, carriesUnresolvedDomain(lhs, rhs)};
}

Interval unknownTruthFrom(const Interval &lhs, const Interval &rhs)
{
    return {0.0, 1.0, carriesUnresolvedDomain(lhs, rhs)};
}

Interval truthIntervalFromCandidates(const bool (&candidates)[4], const bool unresolvedDomain)
{
    return Interval{
        static_cast<double>(candidates[0] && candidates[1] && candidates[2] && candidates[3]),
        static_cast<double>(candidates[0] || candidates[1] || candidates[2] || candidates[3]),
        unresolvedDomain
    };
}
}

bool sameInterval(const Interval &lhs, const Interval &rhs)
{
    return lhs.lower == rhs.lower
        && lhs.upper == rhs.upper
        && lhs.discontinuity == rhs.discontinuity;
}

std::ostream& operator<<(std::ostream& os, const Interval& interval) {
    os << "[" << interval.lower << ", " << interval.upper << "]";
    if (interval.hasDiscontinuity())
    {
        os << "!";
    }
    return os;
}

Interval operator>(const Interval& lhs, const Interval& rhs) {
    if (lhs.undefined() || rhs.undefined())
    {
        return undefinedIntervalFrom(lhs, rhs);
    }

    if (lhs.lower > rhs.upper)
    {
        return {1.0, 1.0, carriesUnresolvedDomain(lhs, rhs)};
    }
    if (lhs.upper <= rhs.lower)
    {
        return {0.0, 0.0, carriesUnresolvedDomain(lhs, rhs)};
    }
    return {0.0, 1.0, carriesUnresolvedDomain(lhs, rhs)};
}

Interval operator<(const Interval& lhs, const Interval& rhs) {
    if (lhs.undefined() || rhs.undefined())
    {
        return undefinedIntervalFrom(lhs, rhs);
    }

    if (lhs.upper < rhs.lower)
    {
        return {1.0, 1.0, carriesUnresolvedDomain(lhs, rhs)};
    }
    if (lhs.lower >= rhs.upper)
    {
        return {0.0, 0.0, carriesUnresolvedDomain(lhs, rhs)};
    }
    return {0.0, 1.0, carriesUnresolvedDomain(lhs, rhs)};
}

Interval operator>=(const Interval& lhs, const Interval& rhs) {
    if (lhs.undefined() || rhs.undefined())
    {
        return undefinedIntervalFrom(lhs, rhs);
    }

    if (lhs.lower >= rhs.upper)
    {
        return {1.0, 1.0, carriesUnresolvedDomain(lhs, rhs)};
    }
    if (lhs.upper < rhs.lower)
    {
        return {0.0, 0.0, carriesUnresolvedDomain(lhs, rhs)};
    }
    return {0.0, 1.0, carriesUnresolvedDomain(lhs, rhs)};
}

Interval operator<=(const Interval& lhs, const Interval& rhs) {
    if (lhs.undefined() || rhs.undefined())
    {
        return undefinedIntervalFrom(lhs, rhs);
    }

    if (lhs.upper <= rhs.lower)
    {
        return {1.0, 1.0, carriesUnresolvedDomain(lhs, rhs)};
    }
    if (lhs.lower > rhs.upper)
    {
        return {0.0, 0.0, carriesUnresolvedDomain(lhs, rhs)};
    }
    return {0.0, 1.0, carriesUnresolvedDomain(lhs, rhs)};
}

Interval operator&&(const Interval& lhs, const Interval& rhs) {
    if (lhs.undefined() || rhs.undefined())
    {
        return unknownTruthFrom(lhs, rhs);
    }

    const bool candidates[] = {
        lhs.lower && rhs.lower,
        lhs.lower && rhs.upper,
        lhs.upper && rhs.lower,
        lhs.upper && rhs.upper
    };

    return truthIntervalFromCandidates(candidates, carriesUnresolvedDomain(lhs, rhs));
}

Interval operator||(const Interval& lhs, const Interval& rhs) {
    if (lhs.undefined() || rhs.undefined())
    {
        return unknownTruthFrom(lhs, rhs);
    }

    const bool candidates[] = {
        lhs.lower || rhs.lower,
        lhs.lower || rhs.upper,
        lhs.upper || rhs.lower,
        lhs.upper || rhs.upper
    };

    return truthIntervalFromCandidates(candidates, carriesUnresolvedDomain(lhs, rhs));
}

Interval operator==(const Interval& lhs, const Interval& rhs) {
    if (lhs.undefined() || rhs.undefined())
    {
        return undefinedIntervalFrom(lhs, rhs);
    }

    if (lhs.upper < rhs.lower || rhs.upper < lhs.lower)
    {
        return {0.0, 0.0, carriesUnresolvedDomain(lhs, rhs)};
    }

    if (lhs.allConstant() && rhs.allConstant() && lhs.lower == rhs.lower)
    {
        return {1.0, 1.0, carriesUnresolvedDomain(lhs, rhs)};
    }

    return {0.0, 1.0, carriesUnresolvedDomain(lhs, rhs)};
}

Interval operator!=(const Interval& lhs, const Interval& rhs) {
    const auto equalRange = lhs == rhs;
    if (equalRange.undefined())
    {
        return equalRange;
    }
    if (equalRange.allTrue())
    {
        return {0.0, 0.0, equalRange.hasDiscontinuity()};
    }

    if (equalRange.allFalse())
    {
        return {1.0, 1.0, equalRange.hasDiscontinuity()};
    }

    return {0.0, 1.0, equalRange.hasDiscontinuity()};
}

Interval operator+(const Interval& lhs, const Interval& rhs) {
    if (lhs.undefined() || rhs.undefined())
    {
        return undefinedIntervalFrom(lhs, rhs);
    }
    return {lhs.lower + rhs.lower, lhs.upper + rhs.upper, carriesUnresolvedDomain(lhs, rhs)};
}

Interval operator-(const Interval& lhs, const Interval& rhs) {
    if (lhs.undefined() || rhs.undefined())
    {
        return undefinedIntervalFrom(lhs, rhs);
    }
    return {lhs.lower - rhs.upper, lhs.upper - rhs.lower, carriesUnresolvedDomain(lhs, rhs)};
}

Interval operator*(const Interval& lhs, const Interval& rhs) {
    if (lhs.undefined() || rhs.undefined())
    {
        return undefinedIntervalFrom(lhs, rhs);
    }

    std::array candidates = {
        lhs.lower * rhs.lower,
        lhs.lower * rhs.upper,
        lhs.upper * rhs.lower,
        lhs.upper * rhs.upper
    };

    double min_val = std::min({ candidates[0], candidates[1], candidates[2], candidates[3] });
    double max_val = std::max({ candidates[0], candidates[1], candidates[2], candidates[3] });

    return Interval(min_val, max_val, carriesUnresolvedDomain(lhs, rhs));
}

Interval operator/(const Interval& lhs, const Interval& rhs) {
    if (lhs.undefined() || rhs.undefined())
    {
        return undefinedIntervalFrom(lhs, rhs);
    }

    if (rhs.lower <= 0.0 && rhs.upper >= 0.0) {
        return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), true};
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

    return Interval(min_val, max_val, carriesUnresolvedDomain(lhs, rhs));
}

namespace
{
double integerPower(const double base, const long long exponent)
{
    auto remaining = exponent;
    auto factor = base;
    auto result = 1.0;
    while (remaining > 0)
    {
        if ((remaining & 1LL) != 0LL)
        {
            result *= factor;
        }
        remaining >>= 1;
        if (remaining > 0)
        {
            factor *= factor;
        }
    }
    return result;
}
}

Interval pow(const Interval& base, const Interval& exp) {
    if (base.undefined() || exp.undefined())
    {
        return undefinedIntervalFrom(base, exp);
    }

    const auto computeIntegerPower = [exponentDiscontinuity = exp.hasDiscontinuity()](
        const Interval &baseInterval,
        const long long exponent) -> Interval
    {
        const auto unresolvedDomain = baseInterval.hasDiscontinuity() || exponentDiscontinuity;
        if (exponent == 0)
        {
            return {1.0, 1.0, unresolvedDomain};
        }

        const auto absExponent = exponent < 0 ? -exponent : exponent;
        const auto lowerMagnitude = integerPower(std::abs(baseInterval.lower), absExponent);
        const auto upperMagnitude = integerPower(std::abs(baseInterval.upper), absExponent);

        Interval powered;
        if ((absExponent & 1LL) == 0LL)
        {
            const auto upper = std::max(lowerMagnitude, upperMagnitude);
            const auto lower = baseInterval.contains(0.0) ? 0.0 : std::min(lowerMagnitude, upperMagnitude);
            powered = Interval{lower, upper};
        }
        else
        {
            const auto endpointLower = std::copysign(lowerMagnitude, baseInterval.lower);
            const auto endpointUpper = std::copysign(upperMagnitude, baseInterval.upper);
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
            return {powered.lower, powered.upper, unresolvedDomain};
        }

        if (powered.contains(0.0))
        {
            return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), true};
        }

        const auto reciprocalLower = 1.0 / powered.upper;
        const auto reciprocalUpper = 1.0 / powered.lower;
        if (reciprocalLower <= reciprocalUpper)
        {
            return {reciprocalLower, reciprocalUpper, unresolvedDomain};
        }

        return {reciprocalUpper, reciprocalLower, unresolvedDomain};
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


    std::array results{
        std::pow(base.lower, exp.lower),
        std::pow(base.lower, exp.upper),
        std::pow(base.upper, exp.lower),
        std::pow(base.upper, exp.upper)
    };

    double min_val = *std::ranges::min_element(results);
    double max_val = *std::ranges::max_element(results);
    return Interval(min_val, max_val, carriesUnresolvedDomain(base, exp));
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
    return lower <= 0 && upper >= 0;
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

bool containsStrictPeriodicPoint(const double lower,
                                 const double upper,
                                 const double phase,
                                 const double period)
{
    const auto k = std::ceil((lower - phase) / period);
    const auto point = phase + k * period;
    return point > lower && point < upper;
}

Interval sin(const Interval& interval) {
    constexpr auto pi = std::numbers::pi;
    constexpr auto fullPeriod = 2.0 * pi;
    if (interval.undefined())
    {
        return undefinedIntervalFrom(interval);
    }

    if (!std::isfinite(interval.lower) || !std::isfinite(interval.upper) || interval.size() >= fullPeriod)
    {
        return {-1.0, 1.0, interval.hasDiscontinuity()};
    }

    auto lower = std::sin(interval.lower);
    auto upper = std::sin(interval.upper);
    if (lower > upper)
    {
        std::swap(lower, upper);
    }
    if (containsStrictPeriodicPoint(interval.lower, interval.upper, pi / 2.0, fullPeriod))
    {
        upper = 1.0;
    }
    if (containsStrictPeriodicPoint(interval.lower, interval.upper, -pi / 2.0, fullPeriod))
    {
        lower = -1.0;
    }
    return {lower, upper, interval.hasDiscontinuity()};
}

Interval cos(const Interval& interval) {
    constexpr auto pi = std::numbers::pi;
    constexpr auto fullPeriod = 2.0 * pi;
    if (interval.undefined())
    {
        return undefinedIntervalFrom(interval);
    }

    if (!std::isfinite(interval.lower) || !std::isfinite(interval.upper) || interval.size() >= fullPeriod)
    {
        return {-1.0, 1.0, interval.hasDiscontinuity()};
    }

    auto lower = std::cos(interval.lower);
    auto upper = std::cos(interval.upper);
    if (lower > upper)
    {
        std::swap(lower, upper);
    }
    if (containsStrictPeriodicPoint(interval.lower, interval.upper, 0.0, fullPeriod))
    {
        upper = 1.0;
    }
    if (containsStrictPeriodicPoint(interval.lower, interval.upper, pi, fullPeriod))
    {
        lower = -1.0;
    }
    return {lower, upper, interval.hasDiscontinuity()};
}

Interval tan(const Interval& interval) {
    constexpr auto pi = std::numbers::pi;
    if (interval.undefined())
    {
        return undefinedIntervalFrom(interval);
    }

    if (!std::isfinite(interval.lower) || !std::isfinite(interval.upper)
        || containsStrictPeriodicPoint(interval.lower, interval.upper, pi / 2.0, pi))
    {
        return Interval(-std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::infinity(),
                        true);
    }

    // tan is strictly increasing where defined, so min and max are at endpoints
    double tan_lower = std::tan(interval.lower);
    double tan_upper = std::tan(interval.upper);
    if (tan_lower < tan_upper) {
        return Interval(tan_lower, tan_upper, interval.hasDiscontinuity());
    } else {
        return Interval(tan_upper, tan_lower, interval.hasDiscontinuity());
    }
}

Interval log(const Interval& interval) {
    if (interval.undefined())
    {
        return undefinedIntervalFrom(interval);
    }

    if (interval.upper <= 0.0) {
        return INTERVAL_UNDEFINED;
    }

    if (interval.lower <= 0.0) {
        return {-std::numeric_limits<double>::infinity(), std::log(interval.upper), true};
    }

    // Log is strictly increasing
    double log_lower = std::log(interval.lower);
    double log_upper = std::log(interval.upper);
    return Interval(log_lower, log_upper, interval.hasDiscontinuity());
}

Interval exp(const Interval& interval) {
    if (interval.undefined())
    {
        return undefinedIntervalFrom(interval);
    }

    // Exp is strictly increasing
    double exp_lower = std::exp(interval.lower);
    double exp_upper = std::exp(interval.upper);
    return Interval(exp_lower, exp_upper, interval.hasDiscontinuity());
}

Interval sqrt(const Interval& interval) {
    if (interval.undefined())
    {
        return undefinedIntervalFrom(interval);
    }

    if (interval.upper < 0.0) {
        return INTERVAL_UNDEFINED;
    }

    if (interval.lower < 0.0) {
        return {0.0, std::sqrt(interval.upper), true};
    }

    // Sqrt is strictly increasing
    double sqrt_lower = std::sqrt(interval.lower);
    double sqrt_upper = std::sqrt(interval.upper);
    return Interval(sqrt_lower, sqrt_upper, interval.hasDiscontinuity());
}
