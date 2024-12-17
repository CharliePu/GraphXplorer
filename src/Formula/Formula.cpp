//
// Created by charl on 6/3/2024.
//

#include "Formula.h"

Formula::Formula(const std::string& expression)
    : expressionStr(expression), tokenizer(expression)
{
    tokens = tokenizer.tokenize();
    if (tokens.empty())
    {
        throw std::invalid_argument("Failed to tokenize expression");
    }
    rpn = parser.convertToRPN(tokens);
    if (rpn.tokens.empty())
    {
        throw std::invalid_argument("Failed to convert to RPN");
    }
}

double Formula::evaluate(const std::unordered_map<std::string, double>& variables) {
    return pointEvaluator.evaluateRPN(rpn, variables);
}

Interval Formula::evaluate(const std::unordered_map<std::string, Interval> &variables)
{
    return intervalEvaluator.evaluateRPN(rpn, variables);
}
