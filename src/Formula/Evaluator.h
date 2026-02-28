//
// Created by charl on 12/13/2024.
//

#ifndef EVALUATOR_H
#define EVALUATOR_H

#include "Token.h"
#include <unordered_map>
#include <stack>
#include <cmath>
#include <stdexcept>

#include "Parser.h"
#include "../Math/Interval.h"

template<typename T>
concept Evaluatable = std::is_arithmetic_v<T> || IsInterval<T>::value;

template<Evaluatable T>
class Evaluator {
public:
    T evaluateRPN(const RPN& rpn, const std::unordered_map<std::string, T>& variables);
private:
    T applyOperator(const std::string& op, T a, T b);
    T applyFunction(const std::string& func, T a);
};

template<Evaluatable T>
T Evaluator<T>::evaluateRPN(const RPN &rpn, const std::unordered_map<std::string, T> &variables)
{
    std::stack<T> evalStack;
    for (const auto& token : rpn.tokens) {
        switch (token.type) {
            case TokenType::NUMBER:
                evalStack.push(static_cast<T>(std::stod(token.value)));
            break;
            case TokenType::VARIABLE:
                if (variables.find(token.value) != variables.end())
                    evalStack.push(variables.at(token.value));
                else
                    throw std::invalid_argument("Undefined variable: " + token.value);
            break;
            case TokenType::OPERATOR: {
                if (evalStack.size() < 2)
                    throw std::invalid_argument("Insufficient operands for operator: " + token.value);
                T b = evalStack.top(); evalStack.pop();
                T a = evalStack.top(); evalStack.pop();
                T res = applyOperator(token.value, a, b);
                evalStack.push(res);
                break;
            }
            case TokenType::FUNCTION: {
                if (evalStack.empty())
                    throw std::invalid_argument("Insufficient operands for function: " + token.value);
                T a = evalStack.top(); evalStack.pop();
                T res = applyFunction(token.value, a);
                evalStack.push(res);
                break;
            }
            default:
                throw std::invalid_argument("Unsupported token type in evaluation.");
        }
    }
    if (evalStack.size() != 1)
        throw std::invalid_argument("Invalid RPN expression.");
    return evalStack.top();
}

template<Evaluatable T>
T Evaluator<T>::applyOperator(const std::string &op, T a, T b)
{
    using namespace std;

    if (op == "+") return a + b;
    if (op == "-") return a - b;
    if (op == "*") return a * b;
    if (op == "/") return a / b;
    if (op == "^") return pow(a, b);
    if (op == ">") return a > b;
    if (op == "<") return a < b;
    if (op == ">=") return a >= b;
    if (op == "<=") return a <= b;
    if (op == "=") return a == b;
    if (op == "!=") return a != b;
    if (op == "&&") return a && b;
    if (op == "||") return a || b;
    // Implement other operators as needed
    throw invalid_argument("Unknown operator: " + op);
}

template<Evaluatable T>
T Evaluator<T>::applyFunction(const std::string &func, T a)
{
    using namespace std;

    if (func == "sin") return sin(a);
    if (func == "cos") return cos(a);
    if (func == "tan") return tan(a);
    if (func == "log") return log(a);
    if (func == "exp") return exp(a);
    if (func == "sqrt") return sqrt(a);
    // Implement other functions as needed
    throw invalid_argument("Unknown function: " + func);
}


#endif //EVALUATOR_H
