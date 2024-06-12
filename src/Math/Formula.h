//
// Created by charl on 6/3/2024.
//

#ifndef FORMULA_H
#define FORMULA_H
#include <string>
#include <vector>

enum TokenType {
    Operator,
    Value,
    Variable
};

struct Token {
    std::string value;
    TokenType type;
};


class Formula {
public:
    explicit Formula(const std::string &formulaStr);
    [[nodiscard]] const std::vector<Token> &getPostfixExpression() const;
    [[nodiscard]] std::string getFormula() const;

    static std::vector<Token> tokenize(const std::string &formulaStr);
    static std::vector<Token> infixToPostfix(const std::vector<Token> &tokens);
private:

    std::string formula;
    std::vector<Token> postfixExpression;
};


inline const std::vector<Token> &Formula::getPostfixExpression() const
{
    return postfixExpression;
}

inline std::string Formula::getFormula() const
{
    return formula;
}


#endif //FORMULA_H
