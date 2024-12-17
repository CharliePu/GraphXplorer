//
// Created by charl on 12/13/2024.
//
#include "catch.hpp"
#include "../src/Formula/Evaluator.h"
#include "../src/Formula/Token.h"
#include "Common.h"

#include <vector>
#include <string>
#include <unordered_map>

TEST_CASE("Evaluator computes simple arithmetic expressions correctly", "[Evaluator][Arithmetic]") {
    Evaluator<double> evaluator;
    RPN rpn = {
        std::vector<Token>{
            Token{TokenType::NUMBER, "2"},
            Token{TokenType::NUMBER, "3"},
            Token{TokenType::OPERATOR, "+"}
        }
    };

    double result = evaluator.evaluateRPN(rpn, {});
    REQUIRE(approxEqual(result, 5.0));
}

TEST_CASE("Evaluator computes expressions with variables correctly", "[Evaluator][Variables]") {
    Evaluator<double> evaluator;
    RPN rpn = {
        std::vector<Token>{
            Token{TokenType::NUMBER, "2"},
            Token{TokenType::VARIABLE, "x"},
            Token{TokenType::OPERATOR, "*"},
            Token{TokenType::NUMBER, "3"},
            Token{TokenType::VARIABLE, "y"},
            Token{TokenType::OPERATOR, "*"},
            Token{TokenType::OPERATOR, "+"}
        }
    };

    std::unordered_map<std::string, double> variables = { {"x", 1.0}, {"y", 2.0} };
    double result = evaluator.evaluateRPN(rpn, variables);
    REQUIRE(approxEqual(result, 2.0 * 1.0 + 3.0 * 2.0)); // 2 + 6 = 8
}

TEST_CASE("Evaluator handles inequalities correctly", "[Evaluator][Inequalities]") {
    Evaluator<double> evaluator;
    RPN rpn = {
        std::vector<Token>{
            Token{TokenType::VARIABLE, "x"},
            Token{TokenType::VARIABLE, "y"},
            Token{TokenType::OPERATOR, ">"}
        }
    };

    // Test Case 1: x > y where x = 3, y = 2 => 1.0
    std::unordered_map<std::string, double> vars1 = { {"x", 3.0}, {"y", 2.0} };
    double result1 = evaluator.evaluateRPN(rpn, vars1);
    REQUIRE(approxEqual(result1, 1.0));

    // Test Case 2: x > y where x = 1, y = 2 => 0.0
    std::unordered_map<std::string, double> vars2 = { {"x", 1.0}, {"y", 2.0} };
    double result2 = evaluator.evaluateRPN(rpn, vars2);
    REQUIRE(approxEqual(result2, 0.0));
}

TEST_CASE("Evaluator computes functions correctly", "[Evaluator][Functions]") {
    Evaluator<double> evaluator;
    RPN rpn = {
        std::vector<Token>{
            Token{TokenType::VARIABLE, "x"},
            Token{TokenType::FUNCTION, "sin"}
        }
    };

    std::unordered_map<std::string, double> variables = { {"x", 1.57079632679} }; // Approximately pi/2
    double result = evaluator.evaluateRPN(rpn, variables);
    REQUIRE(approxEqual(result, 1.0)); // sin(pi/2) = 1
}

TEST_CASE("Evaluator computes complex expressions with functions and operators", "[Evaluator][ComplexExpressions]") {
    Evaluator<double> evaluator;
    RPN rpn = {
        std::vector<Token>{
            Token{TokenType::NUMBER, "3"},
            Token{TokenType::VARIABLE, "x"},
            Token{TokenType::FUNCTION, "sin"},
            Token{TokenType::OPERATOR, "*"}
        }
    };

    std::unordered_map<std::string, double> variables = { {"x", 1.57079632679} }; // pi/2
    double result = evaluator.evaluateRPN(rpn, variables);
    REQUIRE(approxEqual(result, 3.0 * 1.0)); // 3 * sin(pi/2) = 3
}
