//
// Created by charl on 6/3/2024.
//

#ifndef FORMULA_H
#define FORMULA_H

#include <string>
#include <vector>
#include <unordered_map>
#include "Token.h"
#include "Tokenizer.h"
#include "Parser.h"
#include "Evaluator.h"

class Formula {
public:
    Formula(const std::string& expression);
    double evaluate(const std::unordered_map<std::string, double>& variables);
    Interval evaluate(const std::unordered_map<std::string, Interval>& variables);
    [[nodiscard]] const std::string &getExpression() const;
    [[nodiscard]] const RPN &getRPN() const;

private:
    std::string expressionStr;
    std::vector<Token> tokens;
    RPN rpn;
    Tokenizer tokenizer;
    Parser parser;
    Evaluator<double> pointEvaluator;
    Evaluator<Interval> intervalEvaluator;
};

#endif //FORMULA_H
