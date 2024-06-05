//
// Created by charl on 6/3/2024.
//

#ifndef INTERVAL_H
#define INTERVAL_H


template <typename T>
struct Interval {
    T lower;
    T upper;

    [[nodiscard]] double size() const;

    [[nodiscard]] bool contains(const Interval &interval) const;
    [[nodiscard]] bool strictlyContains(const Interval &interval) const;
    [[nodiscard]] bool operator==(const Interval &) const;
};

namespace IntervalValues
{
    static constexpr Interval True{true, true};
    static constexpr Interval False{false, false};
    static constexpr Interval Unknown{false, true};
};

struct ComputeInterval {
    Interval<double> value;
    // TODO: add more fields, definition, continuity, etc.
};

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

#endif //INTERVAL_H
