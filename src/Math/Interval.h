//
// Created by charl on 6/3/2024.
//

#ifndef INTERVAL_H
#define INTERVAL_H
#include <cmath>
#include <cassert>
#include <limits>
#include <ostream>
#include <algorithm>

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
    static constexpr Interval TrueAndFalse{false, true};
    static constexpr Interval Unknown_s{true, false};
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

inline Interval<bool> operator>(const ComputeInterval &lhs, const ComputeInterval &rhs)
{
    const bool candidates[] = {
        lhs.value.lower > rhs.value.lower,
        lhs.value.lower > rhs.value.upper,
        lhs.value.upper > rhs.value.lower,
        lhs.value.upper > rhs.value.upper
    };

    return Interval<bool>{
        candidates[0] && candidates[1] && candidates[2] && candidates[3],
        candidates[0] || candidates[1] || candidates[2] || candidates[3]
    };
}

inline Interval<bool> operator<(const ComputeInterval &lhs, const ComputeInterval &rhs)
{
    const bool candidates[] = {
        lhs.value.lower < rhs.value.lower,
        lhs.value.lower < rhs.value.upper,
        lhs.value.upper < rhs.value.lower,
        lhs.value.upper < rhs.value.upper
    };

    return Interval<bool>{
        candidates[0] && candidates[1] && candidates[2] && candidates[3],
        candidates[0] || candidates[1] || candidates[2] || candidates[3]
    };
}

inline Interval<bool> operator>=(const ComputeInterval &lhs, const ComputeInterval &rhs)
{
    const bool candidates[] = {
        lhs.value.lower >= rhs.value.lower,
        lhs.value.lower >= rhs.value.upper,
        lhs.value.upper >= rhs.value.lower,
        lhs.value.upper >= rhs.value.upper
    };

    return Interval<bool>{
        candidates[0] && candidates[1] && candidates[2] && candidates[3],
        candidates[0] || candidates[1] || candidates[2] || candidates[3]
    };
}


inline Interval<bool> operator<=(const ComputeInterval &lhs, const ComputeInterval &rhs)
{
    const bool candidates[] = {
        lhs.value.lower <= rhs.value.lower,
        lhs.value.lower <= rhs.value.upper,
        lhs.value.upper <= rhs.value.lower,
        lhs.value.upper <= rhs.value.upper
    };

        return Interval<bool>{
            candidates[0] && candidates[1] && candidates[2] && candidates[3],
            candidates[0] || candidates[1] || candidates[2] || candidates[3]
        };
}

inline Interval<bool> operator==(const ComputeInterval &lhs, const ComputeInterval &rhs)
{
    return {lhs.value.lower == rhs.value.lower, lhs.value.upper == rhs.value.upper};
}

inline Interval<bool> operator!(const Interval<bool> &interval)
{
    return {!interval.upper, !interval.lower};
}

inline ComputeInterval operator+(const ComputeInterval &lhs, const ComputeInterval &rhs)
{
    return {{lhs.value.lower + rhs.value.lower, lhs.value.upper + rhs.value.upper}};
}

inline ComputeInterval operator-(const ComputeInterval &lhs, const ComputeInterval &rhs)
{
    return {{lhs.value.lower - rhs.value.lower, lhs.value.upper - rhs.value.upper}};
}

inline ComputeInterval operator*(const ComputeInterval &lhs, const ComputeInterval &rhs)
{
    decltype(lhs.value.lower) candidates[] = {
        lhs.value.lower * rhs.value.lower,
        lhs.value.lower * rhs.value.upper,
        lhs.value.upper * rhs.value.lower,
        lhs.value.upper * rhs.value.upper
    };
    return {
            {
                *std::ranges::min_element(candidates),
                *std::ranges::max_element(candidates)
            }
    };
}

inline ComputeInterval operator/(const ComputeInterval &lhs, const ComputeInterval &rhs)
{
    decltype(lhs.value.lower) candidates[] = {
        lhs.value.lower / rhs.value.lower,
        lhs.value.lower / rhs.value.upper,
        lhs.value.upper / rhs.value.lower,
        lhs.value.upper / rhs.value.upper
    };

    // TODO: handle division by zero

    return {
            {
                *std::ranges::min_element(candidates),
                *std::ranges::max_element(candidates)
            }
    };
}


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
