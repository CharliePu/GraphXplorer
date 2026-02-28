//
// Created by charl on 12/13/2024.
//
#include "catch.hpp"
#include "../src/Formula/Formula.h"
#include "Common.h"
#include <vector>
#include <string>
#include <unordered_map>


TEST_CASE("Formula evaluates simple arithmetic expression correctly", "[Formula][Arithmetic]") {
    std::string formulaStr = "2 + 3 * 4";
    Formula formula(formulaStr);

    
    std::unordered_map<std::string, double> variables{{"x", 0.0}, {"y", 0.0}};
    double result = formula.evaluate(variables);
    REQUIRE(approxEqual(result, 2.0 + 3.0 * 4.0)); // 2 + 12 = 14
}

TEST_CASE("Formula evaluates expressions with variables correctly", "[Formula][Variables]") {
    std::string formulaStr = "2x + 3y";
    Formula formula(formulaStr);


    std::unordered_map<std::string, double> variables{ {"x", 1.0}, {"y", 2.0} };
    double result = formula.evaluate(variables);
    REQUIRE(approxEqual(result, 2.0 * 1.0 + 3.0 * 2.0)); // 2 + 6 = 8
}

TEST_CASE("Formula evaluates expressions with functions correctly", "[Formula][Functions]") {
    std::string formulaStr = "3sin(x)";
    Formula formula(formulaStr);


    std::unordered_map<std::string, double> variables{ {"x", 1.57079632679} }; // pi/2
    double result = formula.evaluate(variables);
    REQUIRE(approxEqual(result, 3.0 * 1.0)); // 3 * sin(pi/2) = 3
}

TEST_CASE("Formula evaluates inequalities correctly", "[Formula][Inequalities]") {
    std::string formulaStr = "x > y";
    Formula formula(formulaStr);

    

    // Test Case 1: x = 3, y = 2 => 1.0
    std::unordered_map<std::string, double> vars1{ {"x", 3.0}, {"y", 2.0} };
    double result1 = formula.evaluate(vars1);
    REQUIRE(approxEqual(result1, 1.0));

    // Test Case 2: x = 1, y = 2 => 0.0
    std::unordered_map<std::string, double> vars2{ {"x", 1.0}, {"y", 2.0} };
    double result2 = formula.evaluate(vars2);
    REQUIRE(approxEqual(result2, 0.0));
}

TEST_CASE("Formula evaluates complex expressions correctly", "[Formula][ComplexExpressions]") {
    std::string formulaStr = "2(x + y) <= 10";
    Formula formula(formulaStr);

    

    // Test Case 1: x = 1, y = 2 => 2*(1 + 2) = 6 <= 10 => 1.0
    std::unordered_map<std::string, double> vars1{ {"x", 1.0}, {"y", 2.0} };
    double result1 = formula.evaluate(vars1);
    REQUIRE(approxEqual(result1, 1.0));

    // Test Case 2: x = 2, y = 3 => 2*(2 + 3) = 10 <= 10 => 1.0
    std::unordered_map<std::string, double> vars2{ {"x", 2.0}, {"y", 3.0} };
    double result2 = formula.evaluate(vars2);
    REQUIRE(approxEqual(result2, 1.0));

    // Test Case 3: x = 3, y = 4 => 2*(3 + 4) = 14 <= 10 => 0.0
    std::unordered_map<std::string, double> vars3{ {"x", 3.0}, {"y", 4.0} };
    double result3 = formula.evaluate(vars3);
    REQUIRE(approxEqual(result3, 0.0));
}

TEST_CASE("Formula evaluates expressions with multiple operators and functions correctly", "[Formula][MultipleOperators][Functions]") {
    std::string formulaStr = "3x + 4sin(y) - z^2";
    Formula formula(formulaStr);

    

    std::unordered_map<std::string, double> variables{ {"x", 2.0}, {"y", 1.57079632679}, {"z", 3.0} };
    double result = formula.evaluate(variables);
    // Calculation: 3*2 + 4*sin(pi/2) - 3^2 = 6 + 4*1 - 9 = 6 + 4 - 9 = 1
    REQUIRE(approxEqual(result, 1.0));
}

TEST_CASE("Formula evaluates expressions", "[Formula][ImplicitMultiplication][Functions]") {
    std::string formulaStr = "2sin(x) + 3cos(y)";
    Formula formula(formulaStr);

    

    std::unordered_map<std::string, double> variables{ {"x", 1.57079632679}, {"y", 0.0} };
    double result = formula.evaluate(variables);
    // Calculation: 2*sin(pi/2) + 3*cos(0) = 2*1 + 3*1 = 5
    REQUIRE(approxEqual(result, 5.0));
}

TEST_CASE("Formula handles complex logical expressions correctly", "[Formula][LogicalExpressions]") {
    std::string formulaStr = "x > y && y > z";
    Formula formula(formulaStr);

    

    // Test Case 1: x=3, y=2, z=1 => (3>2) && (2>1) => 1.0 && 1.0 => 1.0
    std::unordered_map<std::string, double> vars1{ {"x", 3.0}, {"y", 2.0}, {"z", 1.0} };
    double result1 = formula.evaluate(vars1);
    REQUIRE(approxEqual(result1, 1.0));

    // Test Case 2: x=2, y=2, z=1 => (2>2) && (2>1) => 0.0 && 1.0 => 0.0
    std::unordered_map<std::string, double> vars2{ {"x", 2.0}, {"y", 2.0}, {"z", 1.0} };
    double result2 = formula.evaluate(vars2);
    REQUIRE(approxEqual(result2, 0.0));

    // Test Case 3: x=3, y=1, z=2 => (3>1) && (1>2) => 1.0 && 0.0 => 0.0
    std::unordered_map<std::string, double> vars3{ {"x", 3.0}, {"y", 1.0}, {"z", 2.0} };
    double result3 = formula.evaluate(vars3);
    REQUIRE(approxEqual(result3, 0.0));
}

TEST_CASE("Formula detects equality-style operators", "[Formula][Operators]") {
    Formula leFormula("x<=y");
    REQUIRE(leFormula.hasOperatorContainingEqualSign());
    REQUIRE(leFormula.isTopLevelOperator("<="));

    Formula eqFormula("x=y");
    REQUIRE(eqFormula.hasOperatorContainingEqualSign());
    REQUIRE(eqFormula.isTopLevelOperator("="));

    Formula ltFormula("x<y");
    REQUIRE_FALSE(ltFormula.hasOperatorContainingEqualSign());
    REQUIRE(ltFormula.isTopLevelOperator("<"));
}

TEST_CASE("Interval equality is conservative for overlap", "[Formula][Interval][Equality]") {
    Formula eqFormula("x=y");

    const auto disjoint = eqFormula.evaluate({{"x", Interval{0.0, 1.0}}, {"y", Interval{2.0, 3.0}}});
    REQUIRE(disjoint.allFalse());

    const auto overlap = eqFormula.evaluate({{"x", Interval{0.0, 1.0}}, {"y", Interval{0.5, 1.5}}});
    REQUIRE(overlap.lower == 0.0);
    REQUIRE(overlap.upper == 1.0);

    const auto exact = eqFormula.evaluate({{"x", Interval{1.0, 1.0}}, {"y", Interval{1.0, 1.0}}});
    REQUIRE(exact.allTrue());
}

TEST_CASE("Formula uses '=' as equality operator", "[Formula][Operators][Equality]") {
    Formula aliasFormula("x=y^2");
    REQUIRE(aliasFormula.hasOperatorContainingEqualSign());
    REQUIRE(aliasFormula.isTopLevelOperator("="));

    const double trueValue = aliasFormula.evaluate({{"x", 4.0}, {"y", 2.0}});
    REQUIRE(approxEqual(trueValue, 1.0));

    const double falseValue = aliasFormula.evaluate({{"x", 5.0}, {"y", 2.0}});
    REQUIRE(approxEqual(falseValue, 0.0));
}

TEST_CASE("Interval power is conservative when base crosses zero", "[Formula][Interval][Power]") {
    Formula eqFormula("x=y^2");

    // There exists y=0 inside [-1,1], so y^2 includes 0 and equality can be true.
    const auto maybeEqual = eqFormula.evaluate({{"x", Interval{0.0, 0.0}}, {"y", Interval{-1.0, 1.0}}});
    REQUIRE(maybeEqual.lower == 0.0);
    REQUIRE(maybeEqual.upper == 1.0);
}
