//
// Created by charl on 6/3/2024.
//

#ifndef INTERVAL_H
#define INTERVAL_H
#include <cmath>
#include <cassert>
#include <limits>
#include <ostream>

template<typename T>
struct Interval
{
    T lower;
    T upper;

    [[nodiscard]] double size() const;

    [[nodiscard]] bool contains(const Interval &interval) const;

    [[nodiscard]] bool strictlyContains(const Interval &interval) const;

    [[nodiscard]] bool operator==(const Interval &) const;

    [[nodiscard]] bool crossesZero() const;

    Interval<double> operator+(double x) const;
};

namespace IntervalValues
{
    static constexpr Interval True{true, true};
    static constexpr Interval False{false, false};
    static constexpr Interval Unknown{false, true};
};

struct ComputeInterval
{
    Interval<double> value;
    // TODO: add more fields, definition, continuity, etc.
};

template<typename T>
std::ostream& operator<<(std::ostream &os, const Interval<T> &interval)
{
    os << "[" << interval.lower << ", " << interval.upper << "]";
    return os;
}

Interval<bool> operator>(const ComputeInterval &lhs, const ComputeInterval &rhs);

Interval<bool> operator<(const ComputeInterval &lhs, const ComputeInterval &rhs);

Interval<bool> operator>=(const ComputeInterval &lhs, const ComputeInterval &rhs);

Interval<bool> operator<=(const ComputeInterval &lhs, const ComputeInterval &rhs);

Interval<bool> operator==(const ComputeInterval &lhs, const ComputeInterval &rhs);

Interval<bool> operator!(const Interval<bool> &interval);

ComputeInterval operator+(const ComputeInterval &lhs, const ComputeInterval &rhs);

ComputeInterval operator-(const ComputeInterval &lhs, const ComputeInterval &rhs);

ComputeInterval operator*(const ComputeInterval &lhs, const ComputeInterval &rhs);

ComputeInterval operator/(const ComputeInterval &lhs, const ComputeInterval &rhs);


template<typename T>
double Interval<T>::size() const
{
    return upper - lower;
}

template<typename T>
bool Interval<T>::contains(const Interval &interval) const
{
    return lower <= interval.lower && interval.upper <= upper;
}

template<typename T>
bool Interval<T>::strictlyContains(const Interval &interval) const
{
    return lower < interval.lower && interval.upper < upper;
}

template<typename T>
bool Interval<T>::operator==(const Interval &interval) const
{
    return lower == interval.lower && interval.upper == upper;
}

template<>
inline bool Interval<double>::operator==(const Interval &interval) const
{
    return std::abs(lower - interval.lower) < std::numeric_limits<double>::epsilon() && std::abs(interval.upper - upper)
           < std::numeric_limits<double>::epsilon();
}

template<typename T>
bool Interval<T>::crossesZero() const
{
    assert(lower <= upper);
    return (lower < 0 && upper > 0);
}

template<typename T>
Interval<double> Interval<T>::operator+(double x) const
{
    return {lower + x, upper + x};
}

#endif //INTERVAL_H
