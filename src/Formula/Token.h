//
// Created by charl on 12/12/2024.
//

#ifndef TOKEN_H
#define TOKEN_H

#include <string>
#include <stdexcept>

enum class TokenType {
    NUMBER,
    VARIABLE,
    OPERATOR,
    FUNCTION,
    LEFT_PAREN,
    RIGHT_PAREN,
    COMMA,
    NONE
};

inline std::string to_string(TokenType type) {
    switch (type) {
        case TokenType::NUMBER:     return "NUMBER";
        case TokenType::VARIABLE:   return "VARIABLE";
        case TokenType::OPERATOR:   return "OPERATOR";
        case TokenType::FUNCTION:   return "FUNCTION";
        case TokenType::LEFT_PAREN: return "LEFT_PAREN";
        case TokenType::RIGHT_PAREN:return "RIGHT_PAREN";
        case TokenType::COMMA:      return "COMMA";
        case TokenType::NONE:       return "NONE";
        default:
            throw std::invalid_argument("Unknown TokenType");
    }
}



struct Token {
    TokenType type;
    std::string value;
};

#endif //TOKEN_H
