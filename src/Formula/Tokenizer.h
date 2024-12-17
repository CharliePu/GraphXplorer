//
// Created by charl on 12/12/2024.
//

#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "Token.h"
#include <string>
#include <vector>

class Tokenizer {
public:
    Tokenizer(const std::string& expression);
    std::vector<Token> tokenize();

private:
    std::string expr;
    bool isFunction(const std::string& s);
    bool shouldInsertMultiplication(const Token& prev, const Token& current);
};

#endif //TOKENIZER_H
