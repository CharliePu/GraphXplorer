//
// Created by charl on 12/13/2024.
//
#include "catch.hpp"
#include "../src/Formula/Parser.h"
#include "../src/Formula/Token.h"
#include "Common.h"
#include <vector>
#include <string>


TEST_CASE("Parser converts simple infix to postfix correctly", "[Parser][InfixToPostfix]") {
    // Corrected infix tokens: 2 * x + 3 * y
    std::vector<Token> infix = {
        Token{TokenType::NUMBER, "2"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::OPERATOR, "+"},
        Token{TokenType::NUMBER, "3"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::VARIABLE, "y"}
    };

    Parser parser;
    RPN postfix = parser.convertToRPN(infix);

    // Correct expected postfix tokens: 2 x * 3 y * +
    std::vector<Token> expected = {
        Token{TokenType::NUMBER, "2"},
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::NUMBER, "3"},
        Token{TokenType::VARIABLE, "y"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::OPERATOR, "+"}
    };

    REQUIRE(postfix.tokens.size() == expected.size());
    for (size_t i = 0; i < postfix.tokens.size(); ++i) {
        REQUIRE(compareTokens(postfix.tokens[i], expected[i]));
    }
}


TEST_CASE("Parser converts function calls correctly", "[Parser][InfixToPostfix][Functions]") {
    std::vector<Token> infix = {
        Token{TokenType::NUMBER, "3"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::FUNCTION, "sin"},
        Token{TokenType::LEFT_PAREN, "("},
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::RIGHT_PAREN, ")"}
    };

    Parser parser;
    RPN postfix = parser.convertToRPN(infix);

    std::vector<Token> expected = {
        Token{TokenType::NUMBER, "3"},
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::FUNCTION, "sin"},
        Token{TokenType::OPERATOR, "*"}
    };

    REQUIRE(postfix.tokens.size() == expected.size());
    for (size_t i = 0; i < postfix.tokens.size(); ++i) {
        REQUIRE(compareTokens(postfix.tokens[i], expected[i]));
    }
}

TEST_CASE("Parser handles inequalities correctly", "[Parser][InfixToPostfix][Inequalities]") {
    // Corrected infix tokens: (x > y) && (y <= z)
    std::vector<Token> infix = {
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::OPERATOR, ">"},
        Token{TokenType::VARIABLE, "y"},
        Token{TokenType::OPERATOR, "&&"},
        Token{TokenType::VARIABLE, "y"},
        Token{TokenType::OPERATOR, "<="},
        Token{TokenType::VARIABLE, "z"}
    };

    Parser parser;
    RPN postfix = parser.convertToRPN(infix);

    // Correct expected postfix tokens: x y > y z <= &&
    std::vector<Token> expected = {
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::VARIABLE, "y"},
        Token{TokenType::OPERATOR, ">"},
        Token{TokenType::VARIABLE, "y"},
        Token{TokenType::VARIABLE, "z"},
        Token{TokenType::OPERATOR, "<="},
        Token{TokenType::OPERATOR, "&&"}
    };

    REQUIRE(postfix.tokens.size() == expected.size());
    for (size_t i = 0; i < postfix.tokens.size(); ++i) {
        REQUIRE(compareTokens(postfix.tokens[i], expected[i]));
    }
}


TEST_CASE("Parser converts expressions with parentheses correctly", "[Parser][InfixToPostfix][Parentheses]") {
    std::vector<Token> infix = {
        Token{TokenType::NUMBER, "2"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::LEFT_PAREN, "("},
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::OPERATOR, "+"},
        Token{TokenType::VARIABLE, "y"},
        Token{TokenType::RIGHT_PAREN, ")"}
    };

    Parser parser;
    RPN postfix = parser.convertToRPN(infix);

    std::vector<Token> expected = {
        Token{TokenType::NUMBER, "2"},
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::VARIABLE, "y"},
        Token{TokenType::OPERATOR, "+"},
        Token{TokenType::OPERATOR, "*"}
    };

    REQUIRE(postfix.tokens.size() == expected.size());
    for (size_t i = 0; i < postfix.tokens.size(); ++i) {
        REQUIRE(compareTokens(postfix.tokens[i], expected[i]));
    }
}

TEST_CASE("Parser handles '=' equality correctly", "[Parser][InfixToPostfix][Equality]") {
    std::vector<Token> infix = {
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::OPERATOR, "="},
        Token{TokenType::VARIABLE, "y"},
        Token{TokenType::OPERATOR, "^"},
        Token{TokenType::NUMBER, "2"}
    };

    Parser parser;
    RPN postfix = parser.convertToRPN(infix);

    std::vector<Token> expected = {
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::VARIABLE, "y"},
        Token{TokenType::NUMBER, "2"},
        Token{TokenType::OPERATOR, "^"},
        Token{TokenType::OPERATOR, "="}
    };

    REQUIRE(postfix.tokens.size() == expected.size());
    for (size_t i = 0; i < postfix.tokens.size(); ++i) {
        REQUIRE(compareTokens(postfix.tokens[i], expected[i]));
    }
}
