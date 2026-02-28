//
// Created by charl on 12/12/2024.
//
#include "Tokenizer.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <stdexcept>

Tokenizer::Tokenizer(const std::string& expression) : expr(expression) {}

// Define the list of functions you support
bool Tokenizer::isFunction(const std::string& s) {
    static const std::unordered_map<std::string, int> functions = {
        {"sin", 1}, {"cos", 1}, {"tan", 1}, {"log", 1}, {"exp", 1},
        {"sqrt", 1}, {"pow", 2}, {"min", 2}, {"max", 2}
        // Add more functions as needed
    };
    return functions.find(s) != functions.end();
}

// Determine if a multiplication should be inserted between prev and current tokens
bool Tokenizer::shouldInsertMultiplication(const Token& prev, const Token& current) {
    // Define conditions where implicit multiplication occurs
    // 1. Previous token is a number, variable, or right parenthesis
    // 2. Current token is a variable, function, or left parenthesis

    bool prevIsNumber = (prev.type == TokenType::NUMBER);
    bool prevIsVariable = (prev.type == TokenType::VARIABLE);
    bool prevIsRightParen = (prev.type == TokenType::RIGHT_PAREN);

    bool currentIsVariable = (current.type == TokenType::VARIABLE);
    bool currentIsFunction = (current.type == TokenType::FUNCTION);
    bool currentIsLeftParen = (current.type == TokenType::LEFT_PAREN);
    bool currentIsNumber = (current.type == TokenType::NUMBER); // Optional: Handle cases like "x 2"

    if ((prevIsNumber || prevIsVariable || prevIsRightParen) &&
        (currentIsVariable || currentIsFunction || currentIsLeftParen)) {
        return true;
    }

    return false;
}

std::vector<Token> Tokenizer::tokenize() {
    std::vector<Token> tokens;
    size_t i = 0;
    Token previousToken = Token{TokenType::NONE, ""}; // Define TokenType::NONE for initial state

    // Preprocess the expression to all lowercase
    std::string expr = this->expr;
    std::ranges::transform(expr, expr.begin(), ::tolower);

    while (i < expr.length()) {
        char currentChar = expr[i];

        if (std::isspace(currentChar)) {
            ++i;
            continue;
        }

        Token currentToken;

        if (std::isdigit(currentChar) || currentChar == '.') {
            size_t start = i;
            while (i < expr.length() && (std::isdigit(expr[i]) || expr[i] == '.'))
                ++i;
            currentToken = Token{TokenType::NUMBER, expr.substr(start, i - start)};
        }
        else if (std::isalpha(currentChar)) {
            size_t start = i;
            while (i < expr.length() && std::isalpha(expr[i]))
                ++i;
            std::string name = expr.substr(start, i - start);
            if (isFunction(name))
                currentToken = Token{TokenType::FUNCTION, name};
            else
                currentToken = Token{TokenType::VARIABLE, name};
        }
        else {
            // Handle operators, parentheses, commas
            if (currentChar == '=') {
                if (i + 1 < expr.length() && expr[i + 1] == '=') {
                    throw std::invalid_argument("Use single '=' for equality.");
                }
                currentToken = Token{TokenType::OPERATOR, "="};
                ++i;
            }
            else if (currentChar == '>' || currentChar == '<') {
                // Check for two-character comparison operators: >= or <=
                if (i + 1 < expr.length() && (expr[i + 1] == '=')) {
                    currentToken = Token{TokenType::OPERATOR, std::string(1, currentChar) + "="};
                    ++i; // Skip the '=' character
                } else {
                    currentToken = Token{TokenType::OPERATOR, std::string(1, currentChar)};
                }
                ++i;
            }
            else if (currentChar == '&' && i + 1 < expr.length() && expr[i + 1] == '&') {
                currentToken = Token{TokenType::OPERATOR, "&&"};
                i += 2;
            }
            else if (currentChar == '|' && i + 1 < expr.length() && expr[i + 1] == '|') {
                currentToken = Token{TokenType::OPERATOR, "||"};
                i += 2;
            }
            else {
                switch (currentChar) {
                    case '+':
                    case '-':
                    case '*':
                    case '/':
                    case '^':
                        currentToken = Token{TokenType::OPERATOR, std::string(1, currentChar)};
                        break;
                    case '(':
                        currentToken = Token{TokenType::LEFT_PAREN, "("};
                        break;
                    case ')':
                        currentToken = Token{TokenType::RIGHT_PAREN, ")"};
                        break;
                    case ',':
                        currentToken = Token{TokenType::COMMA, ","};
                        break;
                    default:
                        throw std::invalid_argument(std::string("Unknown character: ") + currentChar);
                }
                ++i;
            }
        }

        // Check if we need to insert a multiplication operator
        if (!tokens.empty() && shouldInsertMultiplication(previousToken, currentToken)) {
            tokens.emplace_back(Token{TokenType::OPERATOR, "*"});
        }

        // Add the current token
        tokens.emplace_back(currentToken);
        previousToken = currentToken;
    }

    return tokens;
}
