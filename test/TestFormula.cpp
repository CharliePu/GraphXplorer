#define CATCH_CONFIG_MAIN
#include <catch.hpp>
#include "../src/Math/Formula.h"


TEST_CASE("Tokenize simple formula", "[tokenize]") {
    std::string formula = "3+x*2";
    std::vector<Token> tokens = Formula::tokenize(formula);

    REQUIRE(tokens.size() == 5);
    REQUIRE(tokens[0].value == "3");
    REQUIRE(tokens[0].type == Value);
    REQUIRE(tokens[1].value == "+");
    REQUIRE(tokens[1].type == Operator);
    REQUIRE(tokens[2].value == "x");
    REQUIRE(tokens[2].type == Variable);
    REQUIRE(tokens[3].value == "*");
    REQUIRE(tokens[3].type == Operator);
    REQUIRE(tokens[4].value == "2");
    REQUIRE(tokens[4].type == Value);
}

TEST_CASE("Tokenize complex formula", "[tokenize]") {
    std::string formula = "(3+x)*(2-y)";
    std::vector<Token> tokens = Formula::tokenize(formula);

    REQUIRE(tokens.size() == 11);
    REQUIRE(tokens[0].value == "(");
    REQUIRE(tokens[0].type == Operator);
    REQUIRE(tokens[1].value == "3");
    REQUIRE(tokens[1].type == Value);
    REQUIRE(tokens[2].value == "+");
    REQUIRE(tokens[2].type == Operator);
    REQUIRE(tokens[3].value == "x");
    REQUIRE(tokens[3].type == Variable);
    REQUIRE(tokens[4].value == ")");
    REQUIRE(tokens[4].type == Operator);
    REQUIRE(tokens[5].value == "*");
    REQUIRE(tokens[5].type == Operator);
    REQUIRE(tokens[6].value == "(");
    REQUIRE(tokens[6].type == Operator);
    REQUIRE(tokens[7].value == "2");
    REQUIRE(tokens[7].type == Value);
    REQUIRE(tokens[8].value == "-");
    REQUIRE(tokens[8].type == Operator);
    REQUIRE(tokens[9].value == "y");
    REQUIRE(tokens[9].type == Variable);
    REQUIRE(tokens[10].value == ")");
    REQUIRE(tokens[10].type == Operator);
}

TEST_CASE("Convert simple infix to postfix", "[infixToPostfix]") {
    std::string formula = "3+x*2";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 5);
    REQUIRE(postfixTokens[0].value == "3");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "x");
    REQUIRE(postfixTokens[1].type == Variable);
    REQUIRE(postfixTokens[2].value == "2");
    REQUIRE(postfixTokens[2].type == Value);
    REQUIRE(postfixTokens[3].value == "*");
    REQUIRE(postfixTokens[3].type == Operator);
    REQUIRE(postfixTokens[4].value == "+");
    REQUIRE(postfixTokens[4].type == Operator);
}

TEST_CASE("Convert complex infix to postfix", "[infixToPostfix]") {
    std::string formula = "(3+x)*(2-y)";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 7);
    REQUIRE(postfixTokens[0].value == "3");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "x");
    REQUIRE(postfixTokens[1].type == Variable);
    REQUIRE(postfixTokens[2].value == "+");
    REQUIRE(postfixTokens[2].type == Operator);
    REQUIRE(postfixTokens[3].value == "2");
    REQUIRE(postfixTokens[3].type == Value);
    REQUIRE(postfixTokens[4].value == "y");
    REQUIRE(postfixTokens[4].type == Variable);
    REQUIRE(postfixTokens[5].value == "-");
    REQUIRE(postfixTokens[5].type == Operator);
    REQUIRE(postfixTokens[6].value == "*");
    REQUIRE(postfixTokens[6].type == Operator);
}

TEST_CASE("Handle mismatched parentheses in infix to postfix", "[infixToPostfix]") {
    std::string formula = "(3+x";
    std::vector<Token> tokens = Formula::tokenize(formula);
    REQUIRE_THROWS_AS(Formula::infixToPostfix(tokens), std::invalid_argument);

    formula = "3+x)";
    tokens = Formula::tokenize(formula);
    REQUIRE_THROWS_AS(Formula::infixToPostfix(tokens), std::invalid_argument);
}

TEST_CASE("Handle invalid characters", "[tokenize]") {
    std::string formula = "3+x*z";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);

    formula = "3+@";
    REQUIRE_THROWS_AS(Formula::tokenize(formula), std::invalid_argument);
}

TEST_CASE("Handle variable followed by number", "[tokenize]") {
    std::string formula = "3x";
    std::vector<Token> tokens = Formula::tokenize(formula);
    std::vector<Token> postfixTokens = Formula::infixToPostfix(tokens);

    REQUIRE(postfixTokens.size() == 3);
    REQUIRE(postfixTokens[0].value == "3");
    REQUIRE(postfixTokens[0].type == Value);
    REQUIRE(postfixTokens[1].value == "x");
    REQUIRE(postfixTokens[1].type == Variable);
    REQUIRE(postfixTokens[2].value == "*");
    REQUIRE(postfixTokens[2].type == Operator);
}