//
// Created by charl on 6/3/2024.
//

#include "Formula.h"
#include <cctype>
#include <stack>
#include <stdexcept>
#include <unordered_map>

Formula::Formula(const std::string &formulaStr): formula{formulaStr},
                                                 postfixExpression{infixToPostfix(tokenize(formulaStr))}
{
}

std::vector<Token> Formula::tokenize(std::string formulaStr)
{
    if (formulaStr.empty())
    {
        throw std::invalid_argument("Formula string cannot be empty");
    }

    // Remove all spaces
    std::erase(formulaStr, ' ');

    // Split into tokens
    std::vector<Token> tokens;

    static const auto tokenMap = std::unordered_map<char, std::vector<std::pair<std::string, TokenType> > >{
        {'-', {{"-", Operator}}},
        {'+', {{"+", Operator}}},
        {'*', {{"*", Operator}}},
        {'/', {{"/", Operator}}},
        {'(', {{"(", Bracket}}},
        {')', {{")", Bracket}}},
        {'>', {{">=", Operator}, {">", Operator}}},
        {'<', {{"<=", Operator}, {"<", Operator}}},
        {'=', {{"=", Operator}}},
        {'!', {{"!=", Operator}}},
        {'^', {{"^", Operator}}},
        {'s', {{"sqrt", Function}, {"sin", Function}}},
        {'c', {{"cos", Function}}},
        {'t', {{"tan", Function}}},
        {'l', {{"log", Function}}},
        {'e', {{"exp", Function}, {"e", Value}}},
        {'p', {{"pi", Value}}},
    };

    for (int i = 0; i < formulaStr.size(); i++)
    {
        if (const char c = formulaStr[i]; std::isdigit(c))
        {
            int j = i + 1;
            bool hasDot = false;
            while (j < formulaStr.size() && (std::isdigit(formulaStr[j]) || (formulaStr[j] == '.' && !hasDot)))
            {
                if (formulaStr[j] == '.')
                {
                    hasDot = true;
                }
                j++;
            }

            tokens.emplace_back(formulaStr.substr(i, j - i), Value);
            i = j - 1;
        }
        else if (c == 'x' || c == 'y')
        {
            // Special case: add * operator if a value/variable precedes the variable
            if (i > 0 && (tokens.back().type == Value || tokens.back().type == Variable))
            {
                tokens.emplace_back("*", Operator);
            }

            tokens.emplace_back(std::string{c}, Variable);
        }
        else
        {
            const auto it = tokenMap.find(c);

            if (it == tokenMap.end())
            {
                throw std::invalid_argument("Unrecognized character in formula string: " + std::string{c});
            }

            const std::string_view formulaView(formulaStr);

            for (const auto &[token, type]: it->second)
            {
                if (formulaView.substr(i, token.size()) == token)
                {
                    tokens.emplace_back(token, type);
                    i += token.size() - 1;
                    break;
                }
            }
        }
    }

    // Validity check
    for (int i = 0; i < tokens.size(); i++)
    {
        if (tokens[i].type == Value)
        {
            if (i > 0 && tokens[i - 1].type == Variable)
            {
                throw std::invalid_argument("Values cannot be preceded by operators in formula string");
            }
        }
        else if (tokens[i].type == Operator)
        {
            if (i == 0 || i == tokens.size() - 1)
            {
                throw std::invalid_argument("Operator cannot be at the beginning or end of formula string");
            }

            if (i > 0 && tokens[i - 1].type == Operator)
            {
                throw std::invalid_argument("Operators cannot be adjacent in formula string");
            }
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
        if (op == "^")
        {
            return 4;
        }
        return 0;
    };

    std::stack<Token> opStack;
    std::vector<Token> output;

    for (const Token &token: tokens)
    {
        if (token.type == Value || token.type == Variable)
        {
            output.push_back(token);
        }
        else if (token.type == Bracket)
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
                if (!opStack.empty() && opStack.top().type == Function)
                {
                    output.push_back(opStack.top());
                    opStack.pop();
                }
            }
        }
        else if (token.type == Operator)
        {
            while (!opStack.empty() && precedence(opStack.top().value) >= precedence(token.value))
            {
                // Right associativity for exponentiation
                if (token.value == "^" && opStack.top().value == "^")
                {
                    break;
                }

                output.push_back(opStack.top());
                opStack.pop();
            }
            opStack.push(token);
        }
        else if (token.type == Function)
        {
            opStack.push(token);
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
