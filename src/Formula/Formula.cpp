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

const std::string &Formula::getExpression() const
{
    return expressionStr;
}

const RPN &Formula::getRPN() const
{
    return rpn;
}

bool Formula::hasOperatorContainingEqualSign() const
{
    for (const auto &token : rpn.tokens)
    {
        if (token.type != TokenType::OPERATOR)
        {
            continue;
        }

        if (token.value.find('=') != std::string::npos)
        {
            return true;
        }
    }

    return false;
}

bool Formula::isTopLevelOperator(const std::string &op) const
{
    if (rpn.tokens.empty())
    {
        return false;
    }

    const auto &token = rpn.tokens.back();
    return token.type == TokenType::OPERATOR && token.value == op;
}
