//
// Created by charl on 6/3/2024.
//

#include "Formula.h"
#include <cctype>
#include <stack>
#include <stdexcept>

Formula::Formula(const std::string &formulaStr):
    formula{formulaStr},
    postfixExpression{infixToPostfix(tokenize(formulaStr))}
{
}

std::vector<Token> Formula::tokenize(const std::string &formulaStr)
{
    if (formulaStr.empty())
    {
        throw std::invalid_argument("Formula string cannot be empty");
    }

    auto isOperator = [](char c) -> bool {
        return c == '+' || c == '-' || c == '*' || c == '/' || c == '(' || c == ')' || c == '>' || c == '<' || c == '=';
    };

    std::vector<Token> tokens;

    for (int i = 0; i < formulaStr.size(); i++)
    {
        if (const char c = formulaStr[i]; c == 'x' || c == 'y')
        {
            // 3x=>3*x, xy=>x*y
            if (i > 0 && !isOperator(formulaStr[i - 1]))
            {
                tokens.emplace_back("*", Operator);
            }
            tokens.emplace_back(std::string{c}, Variable);
        }
        else if (std::isdigit(c))
        {
            // x3 => not allowed
            if (i > 0 && (formulaStr[i - 1] == 'x' || formulaStr[i - 1] == 'y'))
            {
                throw std::invalid_argument("Variable cannot be followed by a number");
            }

            int j = i + 1;
            while (j < formulaStr.size() && std::isdigit(formulaStr[j]))
            {
                j++;
            }

            tokens.emplace_back(formulaStr.substr(i, j - i), Value);
            i = j - 1;
        }
        else if (isOperator(c))
        {
            tokens.emplace_back(std::string{c}, Operator);
        }
        else
        {
            throw std::invalid_argument("Unrecognized character in formula string");
        }
    }

    return tokens;
}

std::vector<Token> Formula::infixToPostfix(const std::vector<Token> &tokens)
{
    auto precedence = [](const std::string &op) -> int {
        if (op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=")
        {
            return 1;
        }
        if (op == "+" || op == "-")
        {
            return 2;
        }
        if (op == "*" || op == "/")
        {
            return 3;
        }

        return 0;
    };

    std::stack<Token> opStack;
    std::vector<Token> output;

    for (const Token &token : tokens)
    {
        if (token.type == Value || token.type == Variable)
        {
            output.push_back(token);
        }
        else if (token.type == Operator)
        {
            if (token.value == "(")
            {
                opStack.push(token);
            }
            else if (token.value == ")")
            {
                while (!opStack.empty() && opStack.top().value != "(")
                {
                    output.push_back(opStack.top());
                    opStack.pop();
                }
                if (!opStack.empty())
                {
                    opStack.pop();
                }
                else
                {
                    throw std::invalid_argument("Mismatched parentheses in formula string");
                }
            }
            else
            {
                while (!opStack.empty() && precedence(opStack.top().value) >= precedence(token.value))
                {
                    output.push_back(opStack.top());
                    opStack.pop();
                }
                opStack.push(token);
            }
        }
    }

    while (!opStack.empty())
    {
        if (opStack.top().value == "(" || opStack.top().value == ")")
        {
            throw std::invalid_argument("Mismatched parentheses in formula string");
        }
        output.push_back(opStack.top());
        opStack.pop();
    }

    return output;
}
