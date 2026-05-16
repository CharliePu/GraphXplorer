//
// Created by charl on 6/3/2024.
//

#include "Formula.h"

#include <stdexcept>
#include <utility>

Formula::Formula(const std::string& expression)
    : expressionStr(expression), compiled{std::make_shared<gx::CompiledFormula>()}
{
    auto next = gx::FormulaCompiler{}.compile(expression);
    if (!next.diagnostics.ok)
    {
        throw std::invalid_argument(next.diagnostics.message);
    }
    *compiled = std::move(next);
}

double Formula::evaluate(const std::unordered_map<std::string, double>& variables) {
    return compiled->evaluateDouble(variables);
}

Interval Formula::evaluate(const std::unordered_map<std::string, Interval> &variables)
{
    return compiled->evaluateInterval(variables);
}

const std::string &Formula::getExpression() const
{
    return expressionStr;
}

const RPN &Formula::getRPN() const
{
    return compiled->legacyRpn;
}

const gx::CompiledFormula &Formula::getCompiledFormula() const
{
    return *compiled;
}

bool Formula::hasOperatorContainingEqualSign() const
{
    for (const auto &token : compiled->legacyRpn.tokens)
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
    if (compiled->legacyRpn.tokens.empty())
    {
        return false;
    }

    const auto &token = compiled->legacyRpn.tokens.back();
    return token.type == TokenType::OPERATOR && token.value == op;
}
