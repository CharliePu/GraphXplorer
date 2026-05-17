#ifndef INTERVAL_H
#define INTERVAL_H

#include <limits>
#include <ostream>

struct Interval {
    double lower{0.0};
    double upper{0.0};
    bool discontinuity{false};

    constexpr Interval() = default;
    constexpr Interval(double lower, double upper, bool discontinuity = false):
        lower{lower},
        upper{upper},
        discontinuity{discontinuity}
    {
    }
    constexpr explicit Interval(double value): lower{value}, upper{value}, discontinuity{false} {}

    [[nodiscard]] double size() const;
    [[nodiscard]] bool contains(const Interval& interval) const;
    [[nodiscard]] bool strictlyContains(const Interval& interval) const;
    [[nodiscard]] double mid() const;
    [[nodiscard]] bool allConstant() const;
    [[nodiscard]] bool allIntConst() const;
    [[nodiscard]] constexpr bool contains(double value) const;

    [[nodiscard]] constexpr bool allZero() const;
    [[nodiscard]] constexpr bool allPositive() const;
    [[nodiscard]] constexpr bool allNegative() const;

    [[nodiscard]] bool anyZero() const;
    [[nodiscard]] bool anyPositive() const;
    [[nodiscard]] bool anyNegative() const;

    [[nodiscard]] constexpr bool allTrue() const;
    [[nodiscard]] constexpr bool allFalse() const;
    [[nodiscard]] constexpr bool undefined() const;
    [[nodiscard]] constexpr bool hasDiscontinuity() const;

};

static constexpr auto INTERVAL_UNDEFINED = Interval{1.0, 0.0, true};

constexpr bool Interval::contains(double value) const
{
    return lower <= value && value <= upper;
}

constexpr bool Interval::allZero() const
{
    return lower == 0.0 && upper == 0.0;
}

constexpr bool Interval::allPositive() const
{
    return lower > 0.0 && upper > 0.0;
}

constexpr bool Interval::allNegative() const
{
    return lower < 0.0 && upper < 0.0;
}

constexpr bool Interval::allTrue() const
{
    return lower == 1.0 && upper == 1.0;
}

constexpr bool Interval::allFalse() const
{
    return lower == 0.0 && upper == 0.0;
}

constexpr bool Interval::undefined() const
{
    return lower > upper;
}

constexpr bool Interval::hasDiscontinuity() const
{
    return discontinuity;
}

template<typename T>
struct IsInterval : std::false_type {};

template<>
struct IsInterval<Interval> : std::true_type {};

bool sameInterval(const Interval& lhs, const Interval& rhs);

std::ostream& operator<<(std::ostream& os, const Interval& interval);

Interval operator+(const Interval& lhs, const Interval& rhs);
Interval operator-(const Interval& lhs, const Interval& rhs);
Interval operator*(const Interval& lhs, const Interval& rhs);
Interval operator/(const Interval& lhs, const Interval& rhs);
Interval pow(const Interval& base, const Interval& exp);

Interval operator>(const Interval& lhs, const Interval& rhs);
Interval operator<(const Interval& lhs, const Interval& rhs);
Interval operator>=(const Interval& lhs, const Interval& rhs);
Interval operator<=(const Interval& lhs, const Interval& rhs);
Interval operator==(const Interval& lhs, const Interval& rhs);
Interval operator!=(const Interval& lhs, const Interval& rhs);

Interval operator&&(const Interval& lhs, const Interval& rhs);
Interval operator||(const Interval& lhs, const Interval& rhs);

Interval sin(const Interval& interval);
Interval cos(const Interval& interval);
Interval tan(const Interval& interval);
Interval log(const Interval& interval);
Interval exp(const Interval& interval);
Interval sqrt(const Interval& interval);

#endif // INTERVAL_H
