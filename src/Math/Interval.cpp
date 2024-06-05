//
// Created by charl on 6/3/2024.
//

#include "Interval.h"
#include <algorithm>
#include <iostream>

Interval<bool> operator>(const ComputeInterval &lhs, const ComputeInterval &rhs)
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

Interval<bool> operator<(const ComputeInterval &lhs, const ComputeInterval &rhs)
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

Interval<bool> operator>=(const ComputeInterval &lhs, const ComputeInterval &rhs)
{
    return !(lhs < rhs);
}

Interval<bool> operator<=(const ComputeInterval &lhs, const ComputeInterval &rhs)
{
    return !(lhs > rhs);
}

Interval<bool> operator==(const ComputeInterval &lhs, const ComputeInterval &rhs)
{
    return {lhs.value.lower == rhs.value.lower, lhs.value.upper == rhs.value.upper};
}

Interval<bool> operator!(const Interval<bool> &interval)
{
    return {!interval.upper, !interval.lower};
}

ComputeInterval operator+(const ComputeInterval &lhs, const ComputeInterval &rhs)
{
    return {{lhs.value.lower + rhs.value.lower, lhs.value.upper + rhs.value.upper}};
}

ComputeInterval operator-(const ComputeInterval &lhs, const ComputeInterval &rhs)
{
    return {{lhs.value.lower - rhs.value.lower, lhs.value.upper - rhs.value.upper}};
}

ComputeInterval operator*(const ComputeInterval &lhs, const ComputeInterval &rhs)
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

ComputeInterval operator/(const ComputeInterval &lhs, const ComputeInterval &rhs)
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
