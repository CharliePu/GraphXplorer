//
// Created by charl on 12/12/2024.
//

#include "Parser.h"
#include <stack>
#include <unordered_map>
#include <stdexcept>

struct OperatorInfo {
    int precedence;
    bool rightAssociative;
};

std::unordered_map<std::string, OperatorInfo> operators = {
    {"+", {2, false}}, {"-", {2, false}},
    {"*", {3, false}}, {"/", {3, false}},
    {"^", {4, true}},
    {"<", {1, false}}, {">", {1, false}},
    {"<=", {1, false}}, {">=", {1, false}},
    {"==", {0, false}}, {"!=", {0, false}},
    {"&&", {-1, false}}, {"||", {-2, false}}
    // Add more operators as needed
};

RPN Parser::convertToRPN(const std::vector<Token>& tokens) {
    RPN rpn;
    std::stack<Token> opStack;
    for (const auto& token : tokens) {
        switch (token.type) {
            case TokenType::NUMBER:
            case TokenType::VARIABLE:
                rpn.tokens.push_back(token);
                break;
            case TokenType::FUNCTION:
                opStack.push(token);
                break;
            case TokenType::COMMA:
                while (!opStack.empty() && opStack.top().type != TokenType::LEFT_PAREN) {
                    rpn.tokens.push_back(opStack.top());
                    opStack.pop();
                }
                if (opStack.empty() || opStack.top().type != TokenType::LEFT_PAREN)
                    throw std::invalid_argument("Misplaced comma or parentheses");
                break;
            case TokenType::OPERATOR: {
                auto currentIt = operators.find(token.value);
                if (currentIt == operators.end())
                    throw std::invalid_argument("Unknown operator: " + token.value);
                int currentPrecedence = currentIt->second.precedence;
                bool currentRightAssoc = currentIt->second.rightAssociative;

                while (!opStack.empty()) {
                    Token top = opStack.top();
                    if (top.type != TokenType::OPERATOR)
                        break;
                    auto topIt = operators.find(top.value);
                    if (topIt == operators.end())
                        break;
                    int topPrecedence = topIt->second.precedence;
                    bool topRightAssoc = topIt->second.rightAssociative;

                    if ((topRightAssoc && topPrecedence > currentPrecedence) ||
                        (!topRightAssoc && topPrecedence >= currentPrecedence)) {
                        rpn.tokens.push_back(top);
                        opStack.pop();
                    } else {
                        break;
                    }
                }
                opStack.push(token);
                break;
            }
            case TokenType::LEFT_PAREN:
                opStack.push(token);
                break;
            case TokenType::RIGHT_PAREN:
                while (!opStack.empty() && opStack.top().type != TokenType::LEFT_PAREN) {
                    rpn.tokens.push_back(opStack.top());
                    opStack.pop();
                }
                if (opStack.empty())
                    throw std::invalid_argument("Mismatched parentheses");
                opStack.pop(); // Pop left parenthesis
                if (!opStack.empty() && opStack.top().type == TokenType::FUNCTION) {
                    rpn.tokens.push_back(opStack.top());
                    opStack.pop();
                }
                break;
        }
    }
    while (!opStack.empty()) {
        if (opStack.top().type == TokenType::LEFT_PAREN || opStack.top().type == TokenType::RIGHT_PAREN)
            throw std::invalid_argument("Mismatched parentheses");
        rpn.tokens.push_back(opStack.top());
        opStack.pop();
    }
    return rpn;
}
