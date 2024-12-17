//
// Created by charl on 12/13/2024.
//
#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "../src/Formula/Tokenizer.h"
#include "../src/Formula/Token.h"
#include "Common.h"
#include <vector>
#include <string>

// Existing Test Cases

TEST_CASE("Tokenizer handles implicit multiplication correctly", "[Tokenizer][ImplicitMultiplication]")
{
    std::string formula = "2x + 3y";
    Tokenizer tokenizer(formula);
    std::vector<Token> tokens = tokenizer.tokenize();

    // Expected tokens: 2 (NUMBER), * (OPERATOR), x (VARIABLE), + (OPERATOR),
    // 3 (NUMBER), * (OPERATOR), y (VARIABLE)
    std::vector<Token> expected = {
        Token{TokenType::NUMBER, "2"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::OPERATOR, "+"},
        Token{TokenType::NUMBER, "3"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::VARIABLE, "y"}
    };

    REQUIRE(tokens.size() == expected.size());
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        REQUIRE(compareTokens(tokens[i], expected[i]));
    }
}

TEST_CASE("Tokenizer handles implicit multiplication with parentheses", "[Tokenizer][ImplicitMultiplication]")
{
    std::string formula = "2(x + y)";
    Tokenizer tokenizer(formula);
    std::vector<Token> tokens = tokenizer.tokenize();

    // Expected tokens: 2 (NUMBER), * (OPERATOR), ( (LEFT_PAREN), x (VARIABLE), + (OPERATOR), y (VARIABLE), ) (RIGHT_PAREN)
    std::vector<Token> expected = {
        Token{TokenType::NUMBER, "2"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::LEFT_PAREN, "("},
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::OPERATOR, "+"},
        Token{TokenType::VARIABLE, "y"},
        Token{TokenType::RIGHT_PAREN, ")"}
    };

    REQUIRE(tokens.size() == expected.size());
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        REQUIRE(compareTokens(tokens[i], expected[i]));
    }
}

TEST_CASE("Tokenizer handles implicit multiplication between parentheses", "[Tokenizer][ImplicitMultiplication]")
{
    std::string formula = "(x + y)(a + b)";
    Tokenizer tokenizer(formula);
    std::vector<Token> tokens = tokenizer.tokenize();

    // Expected tokens: ( (LEFT_PAREN), x (VARIABLE), + (OPERATOR), y (VARIABLE), ) (RIGHT_PAREN), * (OPERATOR),
    // ( (LEFT_PAREN), a (VARIABLE), + (OPERATOR), b (VARIABLE), ) (RIGHT_PAREN)
    std::vector<Token> expected = {
        Token{TokenType::LEFT_PAREN, "("},
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::OPERATOR, "+"},
        Token{TokenType::VARIABLE, "y"},
        Token{TokenType::RIGHT_PAREN, ")"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::LEFT_PAREN, "("},
        Token{TokenType::VARIABLE, "a"},
        Token{TokenType::OPERATOR, "+"},
        Token{TokenType::VARIABLE, "b"},
        Token{TokenType::RIGHT_PAREN, ")"}
    };

    REQUIRE(tokens.size() == expected.size());
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        REQUIRE(compareTokens(tokens[i], expected[i]));
    }
}

TEST_CASE("Tokenizer handles functions with implicit multiplication", "[Tokenizer][ImplicitMultiplication][Functions]")
{
    std::string formula = "3sin(x)";
    Tokenizer tokenizer(formula);
    std::vector<Token> tokens = tokenizer.tokenize();

    // Expected tokens: 3 (NUMBER), * (OPERATOR), sin (FUNCTION), ( (LEFT_PAREN), x (VARIABLE), ) (RIGHT_PAREN)
    std::vector<Token> expected = {
        Token{TokenType::NUMBER, "3"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::FUNCTION, "sin"},
        Token{TokenType::LEFT_PAREN, "("},
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::RIGHT_PAREN, ")"}
    };

    REQUIRE(tokens.size() == expected.size());
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        REQUIRE(compareTokens(tokens[i], expected[i]));
    }
}

TEST_CASE("Tokenizer handles inequalities correctly", "[Tokenizer][Inequalities]")
{
    std::string formula = "x > y && y <= z";
    Tokenizer tokenizer(formula);
    std::vector<Token> tokens = tokenizer.tokenize();

    // Expected tokens:
    // x (VARIABLE), > (OPERATOR), y (VARIABLE), && (OPERATOR), y (VARIABLE), <= (OPERATOR), z (VARIABLE)
    std::vector<Token> expected = {
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::OPERATOR, ">"},
        Token{TokenType::VARIABLE, "y"},
        Token{TokenType::OPERATOR, "&&"},
        Token{TokenType::VARIABLE, "y"},
        Token{TokenType::OPERATOR, "<="},
        Token{TokenType::VARIABLE, "z"}
    };

    REQUIRE(tokens.size() == expected.size());
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        REQUIRE(compareTokens(tokens[i], expected[i]));
    }
}

// Additional Test Cases for Enhanced Coverage

TEST_CASE("Tokenizer handles multiple implicit multiplications in a single expression", "[Tokenizer][ImplicitMultiplication]")
{
    std::string formula = "2x(y + 3z)";
    Tokenizer tokenizer(formula);
    std::vector<Token> tokens = tokenizer.tokenize();

    // Corrected expected tokens: includes '*' between '3' and 'z'
    std::vector<Token> expected = {
        Token{TokenType::NUMBER, "2"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::LEFT_PAREN, "("},
        Token{TokenType::VARIABLE, "y"},
        Token{TokenType::OPERATOR, "+"},
        Token{TokenType::NUMBER, "3"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::VARIABLE, "z"},
        Token{TokenType::RIGHT_PAREN, ")"}
    };

    REQUIRE(tokens.size() == expected.size());
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        REQUIRE(compareTokens(tokens[i], expected[i]));
    }
}


TEST_CASE("Tokenizer handles implicit multiplication with exponents", "[Tokenizer][ImplicitMultiplication]")
{
    std::string formula = "2x^2 + 3y";
    Tokenizer tokenizer(formula);
    std::vector<Token> tokens = tokenizer.tokenize();

    // Expected tokens: 2 (NUMBER), * (OPERATOR), x (VARIABLE), ^ (OPERATOR), 2 (NUMBER), + (OPERATOR),
    // 3 (NUMBER), * (OPERATOR), y (VARIABLE)
    std::vector<Token> expected = {
        Token{TokenType::NUMBER, "2"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::OPERATOR, "^"},
        Token{TokenType::NUMBER, "2"},
        Token{TokenType::OPERATOR, "+"},
        Token{TokenType::NUMBER, "3"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::VARIABLE, "y"}
    };

    REQUIRE(tokens.size() == expected.size());
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        REQUIRE(compareTokens(tokens[i], expected[i]));
    }
}

TEST_CASE("Tokenizer handles nested implicit multiplications", "[Tokenizer][ImplicitMultiplication]")
{
    std::string formula = "2(x(y + z))";
    Tokenizer tokenizer(formula);
    std::vector<Token> tokens = tokenizer.tokenize();

    // Expected tokens: 2 (NUMBER), * (OPERATOR), ( (LEFT_PAREN), x (VARIABLE), * (OPERATOR),
    // ( (LEFT_PAREN), y (VARIABLE), + (OPERATOR), z (VARIABLE), ) (RIGHT_PAREN), ) (RIGHT_PAREN)
    std::vector<Token> expected = {
        Token{TokenType::NUMBER, "2"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::LEFT_PAREN, "("},
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::OPERATOR, "*"},
        Token{TokenType::LEFT_PAREN, "("},
        Token{TokenType::VARIABLE, "y"},
        Token{TokenType::OPERATOR, "+"},
        Token{TokenType::VARIABLE, "z"},
        Token{TokenType::RIGHT_PAREN, ")"},
        Token{TokenType::RIGHT_PAREN, ")"}
    };

    REQUIRE(tokens.size() == expected.size());
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        REQUIRE(compareTokens(tokens[i], expected[i]));
    }
}

// Negative Test Cases

TEST_CASE("Tokenizer does not insert implicit multiplication when not applicable", "[Tokenizer][ImplicitMultiplication][Negative]")
{
    std::string formula = "x + y";
    Tokenizer tokenizer(formula);
    std::vector<Token> tokens = tokenizer.tokenize();

    // Expected tokens: x (VARIABLE), + (OPERATOR), y (VARIABLE)
    std::vector<Token> expected = {
        Token{TokenType::VARIABLE, "x"},
        Token{TokenType::OPERATOR, "+"},
        Token{TokenType::VARIABLE, "y"}
    };

    REQUIRE(tokens.size() == expected.size());
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        REQUIRE(compareTokens(tokens[i], expected[i]));
    }
}

TEST_CASE("Tokenizer does not insert implicit multiplication between two numbers", "[Tokenizer][ImplicitMultiplication][Negative]")
{
    std::string formula = "2 3 + x";
    Tokenizer tokenizer(formula);
    std::vector<Token> tokens = tokenizer.tokenize();

    // Depending on your tokenizer's design, this might be invalid.
    // Assuming it tokenizes as two numbers with no multiplication:
    // 2 (NUMBER), 3 (NUMBER), + (OPERATOR), x (VARIABLE)
    std::vector<Token> expected = {
        Token{TokenType::NUMBER, "2"},
        Token{TokenType::NUMBER, "3"},
        Token{TokenType::OPERATOR, "+"},
        Token{TokenType::VARIABLE, "x"}
    };

    REQUIRE(tokens.size() == expected.size());
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        REQUIRE(compareTokens(tokens[i], expected[i]));
    }
}

TEST_CASE("Tokenizer handles unknown characters gracefully", "[Tokenizer][ErrorHandling]")
{
    std::string formula = "2x + @y";
    Tokenizer tokenizer(formula);

    REQUIRE_THROWS_AS(tokenizer.tokenize(), std::invalid_argument);
}

