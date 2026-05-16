//
// Created by charl on 6/3/2024.
//

#ifndef FORMULA_H
#define FORMULA_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "Token.h"
#include "Parser.h"
#include "FormulaCompiler.h"

class Formula {
public:
    Formula(const std::string& expression);
    double evaluate(const std::unordered_map<std::string, double>& variables);
    Interval evaluate(const std::unordered_map<std::string, Interval>& variables);
    [[nodiscard]] const std::string &getExpression() const;
    [[nodiscard]] const RPN &getRPN() const;
    [[nodiscard]] const gx::CompiledFormula &getCompiledFormula() const;
    [[nodiscard]] bool hasOperatorContainingEqualSign() const;
    [[nodiscard]] bool isTopLevelOperator(const std::string &op) const;

private:
    std::string expressionStr;
    std::shared_ptr<gx::CompiledFormula> compiled;
};

#endif //FORMULA_H
