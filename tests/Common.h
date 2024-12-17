//
// Created by charl on 12/15/2024.
//

#ifndef COMMON_H
#define COMMON_H

#include <cmath>
#include <iostream>
#include "../src/Formula/Token.h"

// Helper function to compare tokens
inline bool compareTokens(const Token &a, const Token &b)
{
    if (a.type != b.type || a.value != b.value)
    {
        std::cout << "Expected: " << to_string(a.type) << " " << a.value << std::endl;
        std::cout << "Actual: " << to_string(b.type) << " " << b.value << std::endl;
        return false;
    }
    return true;
}

// Helper function to compare floating-point numbers with tolerance
inline bool approxEqual(double a, double b, double epsilon = 1e-5) {
    if (std::abs(a - b) >= epsilon) {
        std::cout << "Expected: " << a << std::endl;
        std::cout << "Actual: " << b << std::endl;
    }
    return std::abs(a - b) < epsilon;
}

#endif //COMMON_H
