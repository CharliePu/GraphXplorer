/*
 * TestFormula.cpp
 *
 *  AI generated testcases
 *
 *
 */

#define CATCH_CONFIG_MAIN
#include "../src/Math/Formula.h"

#define CATCH_CONFIG_MAIN
#include <catch.hpp>
#include "../src/Math/Formula.h"

// Valid Inputs

TEST_CASE("Tokenize and convert simple addition formula", "[tokenize][infixToPostfix]") {
    std::string formula = "5+3";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "5");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "3");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "+");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert simple subtraction formula", "[tokenize][infixToPostfix]") {
    std::string formula = "27-14";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "27");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "14");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "-");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert simple multiplication formula", "[tokenize][infixToPostfix]") {
    std::string formula = "43*19";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "43");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "19");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "*");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert simple division formula", "[tokenize][infixToPostfix]") {
    std::string formula = "81/9";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "81");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "9");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "/");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with greater than operator", "[tokenize][infixToPostfix]") {
    std::string formula = "15 > 3";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "15");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "3");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == ">");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with less than operator", "[tokenize][infixToPostfix]") {
    std::string formula = "22 < 14";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "22");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "14");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "<");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with greater than or equal to operator", "[tokenize][infixToPostfix]") {
    std::string formula = "33 >= 33";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "33");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "33");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == ">=");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with less than or equal to operator", "[tokenize][infixToPostfix]") {
    std::string formula = "55 <= 56";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "55");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "56");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "<=");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with not equal to operator", "[tokenize][infixToPostfix]") {
    std::string formula = "44 != 45";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "44");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "45");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "!=");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with equal to operator", "[tokenize][infixToPostfix]") {
    std::string formula = "66 = 66";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "66");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "66");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "=");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with simple exponentiation", "[tokenize][infixToPostfix]") {
    std::string formula = "2^3";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "2");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "3");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "^");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with variable exponentiation", "[tokenize][infixToPostfix]") {
    std::string formula = "x^y";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "x");
    REQUIRE(postfixTokens[0].type == Variable);
    REQUIRE(postfixTokens[1].value == "y");
    REQUIRE(postfixTokens[1].type == Variable);
    REQUIRE(postfixTokens[2].value == "^");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with large numbers in exponentiation", "[tokenize][infixToPostfix]") {
    std::string formula = "22^33";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "22");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "33");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "^");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with nested exponentiation", "[tokenize][infixToPostfix]") {
    std::string formula = "2^3^2";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 5);
    REQUIRE(postfixTokens[0].value == "2");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "3");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "2");
    REQUIRE(postfixTokens[2].type == Value);
    REQUIRE(postfixTokens[3].value == "^");
    REQUIRE(postfixTokens[3].type == Operator);
    REQUIRE(postfixTokens[4].value == "^");
    REQUIRE(postfixTokens[4].type == Operator);
}

TEST_CASE("Tokenize and convert formula with floating point addition", "[tokenize][infixToPostfix]") {
    std::string formula = "3.14 + 1.86";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "3.14");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "1.86");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "+");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with floating point multiplication", "[tokenize][infixToPostfix]") {
    std::string formula = "2.57 * 4.25";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "2.57");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "4.25");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "*");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with floating point division", "[tokenize][infixToPostfix]") {
    std::string formula = "6.03 / 2.07";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "6.03");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "2.07");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "/");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with floating point subtraction", "[tokenize][infixToPostfix]") {
    std::string formula = "1.58 - 0.52";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "1.58");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "0.52");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "-");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with mixed addition and multiplication", "[tokenize][infixToPostfix]") {
    std::string formula = "3+5*2";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 5);
    REQUIRE(postfixTokens[0].value == "3");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "5");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "2");
    REQUIRE(postfixTokens[2].type == Value);
    REQUIRE(postfixTokens[3].value == "*");
    REQUIRE(postfixTokens[3].type == Operator);
    REQUIRE(postfixTokens[4].value == "+");
    REQUIRE(postfixTokens[4].type == Operator);
}

TEST_CASE("Tokenize and convert formula with parentheses", "[tokenize][infixToPostfix]") {
    std::string formula = "(4+5)*6";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 5);
    REQUIRE(postfixTokens[0].value == "4");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "5");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "+");
    REQUIRE(postfixTokens[2].type == Operator);
    REQUIRE(postfixTokens[3].value == "6");
    REQUIRE(postfixTokens[3].type == Value);
    REQUIRE(postfixTokens[4].value == "*");
    REQUIRE(postfixTokens[4].type == Operator);
}

TEST_CASE("Tokenize and convert formula with exponentiation and addition", "[tokenize][infixToPostfix]") {
    std::string formula = "2^(3+1)";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 5);
    REQUIRE(postfixTokens[0].value == "2");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "3");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "1");
    REQUIRE(postfixTokens[2].type == Value);
    REQUIRE(postfixTokens[3].value == "+");
    REQUIRE(postfixTokens[3].type == Operator);
    REQUIRE(postfixTokens[4].value == "^");
    REQUIRE(postfixTokens[4].type == Operator);
}

TEST_CASE("Tokenize and convert formula with multiple operations", "[tokenize][infixToPostfix]") {
    std::string formula = "(10+21)*(31-14)/5";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 9);
    REQUIRE(postfixTokens[0].value == "10");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "21");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "+");
    REQUIRE(postfixTokens[2].type == Operator);
    REQUIRE(postfixTokens[3].value == "31");
    REQUIRE(postfixTokens[3].type == Value);
    REQUIRE(postfixTokens[4].value == "14");
    REQUIRE(postfixTokens[4].type == Value);
    REQUIRE(postfixTokens[5].value == "-");
    REQUIRE(postfixTokens[5].type == Operator);
    REQUIRE(postfixTokens[6].value == "*");
    REQUIRE(postfixTokens[6].type == Operator);
    REQUIRE(postfixTokens[7].value == "5");
    REQUIRE(postfixTokens[7].type == Value);
    REQUIRE(postfixTokens[8].value == "/");
    REQUIRE(postfixTokens[8].type == Operator);
}

TEST_CASE("Tokenize and convert formula with sin function", "[tokenize][infixToPostfix]") {
    std::string formula = "sin(0)";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 2);
    REQUIRE(postfixTokens[0].value == "0");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "sin");
    REQUIRE(postfixTokens[1].type == Function);
}

TEST_CASE("Tokenize and convert formula with cos function", "[tokenize][infixToPostfix]") {
    std::string formula = "cos(3.14)";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 2);
    REQUIRE(postfixTokens[0].value == "3.14");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "cos");
    REQUIRE(postfixTokens[1].type == Function);
}

TEST_CASE("Tokenize and convert formula with tan function", "[tokenize][infixToPostfix]") {
    std::string formula = "tan(0.785)";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 2);
    REQUIRE(postfixTokens[0].value == "0.785");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "tan");
    REQUIRE(postfixTokens[1].type == Function);
}

TEST_CASE("Tokenize and convert formula with sqrt function", "[tokenize][infixToPostfix]") {
    std::string formula = "sqrt(4)";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 2);
    REQUIRE(postfixTokens[0].value == "4");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "sqrt");
    REQUIRE(postfixTokens[1].type == Function);
}

TEST_CASE("Tokenize and convert formula with log function", "[tokenize][infixToPostfix]") {
    std::string formula = "log(10)";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 2);
    REQUIRE(postfixTokens[0].value == "10");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "log");
    REQUIRE(postfixTokens[1].type == Function);
}

TEST_CASE("Tokenize and convert formula with exp function", "[tokenize][infixToPostfix]") {
    std::string formula = "exp(1)";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 2);
    REQUIRE(postfixTokens[0].value == "1");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "exp");
    REQUIRE(postfixTokens[1].type == Function);
}

TEST_CASE("Tokenize and convert formula with variables and addition", "[tokenize][infixToPostfix]") {
    std::string formula = "2x + 3y";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 7);
    REQUIRE(postfixTokens[0].value == "2");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "x");
    REQUIRE(postfixTokens[1].type == Variable);
    REQUIRE(postfixTokens[2].value == "*");
    REQUIRE(postfixTokens[2].type == Operator);
    REQUIRE(postfixTokens[3].value == "3");
    REQUIRE(postfixTokens[3].type == Value);
    REQUIRE(postfixTokens[4].value == "y");
    REQUIRE(postfixTokens[4].type == Variable);
    REQUIRE(postfixTokens[5].value == "*");
    REQUIRE(postfixTokens[5].type == Operator);
    REQUIRE(postfixTokens[6].value == "+");
    REQUIRE(postfixTokens[6].type == Operator);
}

TEST_CASE("Tokenize and convert formula with pi constant", "[tokenize][infixToPostfix]") {
    std::string formula = "pi*4^2";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 5);
    REQUIRE(postfixTokens[0].value == "pi");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "4");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "2");
    REQUIRE(postfixTokens[2].type == Value);
    REQUIRE(postfixTokens[3].value == "^");
    REQUIRE(postfixTokens[3].type == Operator);
    REQUIRE(postfixTokens[4].value == "*");
    REQUIRE(postfixTokens[4].type == Operator);
}

TEST_CASE("Tokenize and convert formula with e constant and variable exponentiation", "[tokenize][infixToPostfix]") {
    std::string formula = "e^x";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "e");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "x");
    REQUIRE(postfixTokens[1].type == Variable);
    REQUIRE(postfixTokens[2].value == "^");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with variable concatenation", "[tokenize][infixToPostfix]") {
    std::string formula = "2x";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "2");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "x");
    REQUIRE(postfixTokens[1].type == Variable);
    REQUIRE(postfixTokens[2].value == "*");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with multiple variable concatenation", "[tokenize][infixToPostfix]") {
    std::string formula = "2xy";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 5);
    REQUIRE(postfixTokens[0].value == "2");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "x");
    REQUIRE(postfixTokens[1].type == Variable);
    REQUIRE(postfixTokens[2].value == "*");
    REQUIRE(postfixTokens[2].type == Operator);
    REQUIRE(postfixTokens[3].value == "y");
    REQUIRE(postfixTokens[3].type == Variable);
    REQUIRE(postfixTokens[4].value == "*");
    REQUIRE(postfixTokens[4].type == Operator);
}

TEST_CASE("Tokenize and convert formula with complex variable concatenation", "[tokenize][infixToPostfix]") {
    std::string formula = "xxxyyy";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 11);
    REQUIRE(postfixTokens[0].value == "x");
    REQUIRE(postfixTokens[0].type == Variable);
    REQUIRE(postfixTokens[1].value == "x");
    REQUIRE(postfixTokens[1].type == Variable);
    REQUIRE(postfixTokens[2].value == "*");
    REQUIRE(postfixTokens[2].type == Operator);
    REQUIRE(postfixTokens[3].value == "x");
    REQUIRE(postfixTokens[3].type == Variable);
    REQUIRE(postfixTokens[4].value == "*");
    REQUIRE(postfixTokens[4].type == Operator);
    REQUIRE(postfixTokens[5].value == "y");
    REQUIRE(postfixTokens[5].type == Variable);
    REQUIRE(postfixTokens[6].value == "*");
    REQUIRE(postfixTokens[6].type == Operator);
    REQUIRE(postfixTokens[7].value == "y");
    REQUIRE(postfixTokens[7].type == Variable);
    REQUIRE(postfixTokens[8].value == "*");
    REQUIRE(postfixTokens[8].type == Operator);
    REQUIRE(postfixTokens[9].value == "y");
    REQUIRE(postfixTokens[9].type == Variable);
    REQUIRE(postfixTokens[10].value == "*");
    REQUIRE(postfixTokens[10].type == Operator);
}

TEST_CASE("Tokenize formula with mismatched opening parenthesis", "[tokenize]") {
    std::string formula = "(3+5";
    auto tokens = Formula::tokenize(formula);
    REQUIRE_THROWS_AS(Formula::infixToPostfix(tokens), std::invalid_argument);
}

TEST_CASE("Tokenize formula with mismatched closing parenthesis", "[tokenize]") {
    std::string formula = "4+2)";
    auto tokens = Formula::tokenize(formula);
    REQUIRE_THROWS_AS(Formula::infixToPostfix(tokens), std::invalid_argument);
}


TEST_CASE("Tokenize formula with invalid character @", "[tokenize]") {
    std::string formula = "3+@";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with invalid character #", "[tokenize]") {
    std::string formula = "5#2";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with invalid character &", "[tokenize]") {
    std::string formula = "a&b";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with invalid variable z", "[tokenize]") {
    std::string formula = "2z";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with invalid string abc", "[tokenize]") {
    std::string formula = "2 * abc";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with multiple plus operators", "[tokenize]") {
    std::string formula = "3++5";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with multiple operators", "[tokenize]") {
    std::string formula = "6*/2";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with multiple minus operators", "[tokenize]") {
    std::string formula = "7--8";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with operator at start", "[tokenize]") {
    std::string formula = "+3*5";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with operator at end", "[tokenize]") {
    std::string formula = "4/2-";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with invalid function name sinus", "[tokenize]") {
    std::string formula = "sinus(0)";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with invalid function name cosine", "[tokenize]") {
    std::string formula = "cosine(3.14)";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with missing operator between variables and number", "[tokenize]") {
    std::string formula = "5y2";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with invalid floating point format (double dot)", "[tokenize]") {
    std::string formula = "3..14 + 1.86";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with invalid floating point format (multiple dots)", "[tokenize]") {
    std::string formula = "2.5.4 * 4.2";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with invalid exponent operator", "[tokenize]") {
    std::string formula = "2^^3";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with missing exponent value", "[tokenize]") {
    std::string formula = "2^";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with invalid comparison operator ><", "[tokenize]") {
    std::string formula = "5 >< 3";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with invalid comparison operator =>", "[tokenize]") {
    std::string formula = "4 => 2";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize and convert formula with complex nested operations", "[tokenize][infixToPostfix]") {
    std::string formula = "((3+5)*2)^(1+1)";
    std::vector<Token> tokens = Formula::tokenize(formula);

    auto postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 9);
    REQUIRE(postfixTokens[0].value == "3");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "5");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "+");
    REQUIRE(postfixTokens[2].type == Operator);
    REQUIRE(postfixTokens[3].value == "2");
    REQUIRE(postfixTokens[3].type == Value);
    REQUIRE(postfixTokens[4].value == "*");
    REQUIRE(postfixTokens[4].type == Operator);
    REQUIRE(postfixTokens[5].value == "1");
    REQUIRE(postfixTokens[5].type == Value);
    REQUIRE(postfixTokens[6].value == "1");
    REQUIRE(postfixTokens[6].type == Value);
    REQUIRE(postfixTokens[7].value == "+");
    REQUIRE(postfixTokens[7].type == Operator);
    REQUIRE(postfixTokens[8].value == "^");
    REQUIRE(postfixTokens[8].type == Operator);
}


TEST_CASE("Tokenize and convert formula with floating points and exponents combined", "[tokenize][infixToPostfix]") {
    std::string formula = "3.14^2";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "3.14");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "2");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "^");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with FP and exponents combined", "[tokenize][infixToPostfix]") {
    std::string formula = "2.71^3.14";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "2.71");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "3.14");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "^");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with nested sin and cos functions", "[tokenize][infixToPostfix]") {
    std::string formula = "sin(cos(3.14))";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "3.14");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "cos");
    REQUIRE(postfixTokens[1].type == Function);
    REQUIRE(postfixTokens[2].value == "sin");
    REQUIRE(postfixTokens[2].type == Function);
}

TEST_CASE("Tokenize and convert formula with nested log and exp functions", "[tokenize][infixToPostfix]") {
    std::string formula = "log(exp(1))";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "1");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "exp");
    REQUIRE(postfixTokens[1].type == Function);
    REQUIRE(postfixTokens[2].value == "log");
    REQUIRE(postfixTokens[2].type == Function);
}

TEST_CASE("Tokenize and convert formula with nested sqrt and exponentiation", "[tokenize][infixToPostfix]") {
    std::string formula = "sqrt(2^2)";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 4);
    REQUIRE(postfixTokens[0].value == "2");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "2");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "^");
    REQUIRE(postfixTokens[2].type == Operator);
    REQUIRE(postfixTokens[3].value == "sqrt");
    REQUIRE(postfixTokens[3].type == Function);
}

TEST_CASE("Tokenize and convert formula combining all features", "[tokenize][infixToPostfix]") {
    std::string formula = "sin(3.14) + cos(0) > log(1)";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 8);
    REQUIRE(postfixTokens[0].value == "3.14");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "sin");
    REQUIRE(postfixTokens[1].type == Function);
    REQUIRE(postfixTokens[2].value == "0");
    REQUIRE(postfixTokens[2].type == Value);
    REQUIRE(postfixTokens[3].value == "cos");
    REQUIRE(postfixTokens[3].type == Function);
    REQUIRE(postfixTokens[4].value == "+");
    REQUIRE(postfixTokens[4].type == Operator);
    REQUIRE(postfixTokens[5].value == "1");
    REQUIRE(postfixTokens[5].type == Value);
    REQUIRE(postfixTokens[6].value == "log");
    REQUIRE(postfixTokens[6].type == Function);
    REQUIRE(postfixTokens[7].value == ">");
    REQUIRE(postfixTokens[7].type == Operator);
}

TEST_CASE("Tokenize formula with invalid parentheses and sin function", "[tokenize]") {
    std::string formula = "sin(3.14";
    auto tokens = Formula::tokenize(formula);
    REQUIRE_THROWS_AS(Formula::infixToPostfix(tokens), std::invalid_argument);
}

TEST_CASE("Tokenize formula with invalid parentheses and cos function", "[tokenize]") {
    std::string formula = "cos 3.14)";
    auto tokens = Formula::tokenize(formula);
    REQUIRE_THROWS_AS(Formula::infixToPostfix(tokens), std::invalid_argument);
}

TEST_CASE("Tokenize formula with invalid nested parentheses in sqrt function", "[tokenize]") {
    std::string formula = "sqrt(4))";
    auto tokens = Formula::tokenize(formula);
    REQUIRE_THROWS_AS(Formula::infixToPostfix(tokens), std::invalid_argument);
}

TEST_CASE("Tokenize formula with variable starting with number 1var", "[tokenize]") {
    std::string formula = "1var + 3";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with variable starting with number 3num", "[tokenize]") {
    std::string formula = "3num * 2";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize formula with invalid base for exponent", "[tokenize]") {
    std::string formula = "^2";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Tokenize and convert formula with valid variable concatenation 2x", "[tokenize][infixToPostfix]") {
    std::string formula = "2x";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "2");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "x");
    REQUIRE(postfixTokens[1].type == Variable);
    REQUIRE(postfixTokens[2].value == "*");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with valid variable concatenation 2xy", "[tokenize][infixToPostfix]") {
    std::string formula = "2xy";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 5);
    REQUIRE(postfixTokens[0].value == "2");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "x");
    REQUIRE(postfixTokens[1].type == Variable);
    REQUIRE(postfixTokens[2].value == "*");
    REQUIRE(postfixTokens[2].type == Operator);
    REQUIRE(postfixTokens[3].value == "y");
    REQUIRE(postfixTokens[3].type == Variable);
    REQUIRE(postfixTokens[4].value == "*");
    REQUIRE(postfixTokens[4].type == Operator);
}

TEST_CASE("Tokenize and convert formula with valid variable concatenation xxxyyy", "[tokenize][infixToPostfix]") {
    std::string formula = "xxxyyy";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 11);
    REQUIRE(postfixTokens[0].value == "x");
    REQUIRE(postfixTokens[0].type == Variable);
    REQUIRE(postfixTokens[1].value == "x");
    REQUIRE(postfixTokens[1].type == Variable);
    REQUIRE(postfixTokens[2].value == "*");
    REQUIRE(postfixTokens[2].type == Operator);
    REQUIRE(postfixTokens[3].value == "x");
    REQUIRE(postfixTokens[3].type == Variable);
    REQUIRE(postfixTokens[4].value == "*");
    REQUIRE(postfixTokens[4].type == Operator);
    REQUIRE(postfixTokens[5].value == "y");
    REQUIRE(postfixTokens[5].type == Variable);
    REQUIRE(postfixTokens[6].value == "*");
    REQUIRE(postfixTokens[6].type == Operator);
    REQUIRE(postfixTokens[7].value == "y");
    REQUIRE(postfixTokens[7].type == Variable);
    REQUIRE(postfixTokens[8].value == "*");
    REQUIRE(postfixTokens[8].type == Operator);
    REQUIRE(postfixTokens[9].value == "y");
    REQUIRE(postfixTokens[9].type == Variable);
    REQUIRE(postfixTokens[10].value == "*");
    REQUIRE(postfixTokens[10].type == Operator);
}

TEST_CASE("Tokenize and convert formula with multiple digits in addition", "[tokenize][infixToPostfix]") {
    std::string formula = "123 + 456";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "123");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "456");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "+");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with multiple digits in subtraction", "[tokenize][infixToPostfix]") {
    std::string formula = "789 - 123";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "789");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "123");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "-");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with multiple digits in multiplication", "[tokenize][infixToPostfix]") {
    std::string formula = "456 * 789";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "456");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "789");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "*");
    REQUIRE(postfixTokens[2].type == Operator);
}

TEST_CASE("Tokenize and convert formula with multiple digits in division", "[tokenize][infixToPostfix]") {
    std::string formula = "123456 / 789";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "123456");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "789");
    REQUIRE(postfixTokens[1].type == Value);
    REQUIRE(postfixTokens[2].value == "/");
    REQUIRE(postfixTokens[2].type == Operator);
}
