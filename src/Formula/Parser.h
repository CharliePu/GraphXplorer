//
// Created by charl on 12/12/2024.
//

#ifndef PARSER_H
#define PARSER_H
#include <vector>

#include "Token.h"


struct RPN {
    std::vector<Token> tokens;
};

class Parser {
public:
    RPN convertToRPN(const std::vector<Token>& tokens);
private:
    // Operator precedence and associativity
    // Helper methods
};


#endif //PARSER_H
