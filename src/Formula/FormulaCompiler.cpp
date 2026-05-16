#include "FormulaCompiler.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "../Util/StableHash.h"
#include "Tokenizer.h"

namespace gx
{
struct FormulaCompiler::AstNode
{
    Token token{};
    std::vector<std::unique_ptr<AstNode>> children{};
    std::string canonical{};
};

namespace
{
template<typename T>
T applyBinary(const FormulaOp op, const T &a, const T &b)
{
    using namespace std;
    switch (op)
    {
    case FormulaOp::Add:
        return a + b;
    case FormulaOp::Subtract:
        return a - b;
    case FormulaOp::Multiply:
        return a * b;
    case FormulaOp::Divide:
        return a / b;
    case FormulaOp::Power:
        return pow(a, b);
    case FormulaOp::Greater:
        return a > b;
    case FormulaOp::Less:
        return a < b;
    case FormulaOp::GreaterEqual:
        return a >= b;
    case FormulaOp::LessEqual:
        return a <= b;
    case FormulaOp::Equal:
        return a == b;
    case FormulaOp::NotEqual:
        return a != b;
    case FormulaOp::And:
        return a && b;
    case FormulaOp::Or:
        return a || b;
    default:
        throw std::invalid_argument("Opcode is not binary");
    }
}

template<typename T>
T applyUnary(const FormulaOp op, const T &a)
{
    using namespace std;
    switch (op)
    {
    case FormulaOp::Sin:
        return sin(a);
    case FormulaOp::Cos:
        return cos(a);
    case FormulaOp::Tan:
        return tan(a);
    case FormulaOp::Log:
        return log(a);
    case FormulaOp::Exp:
        return exp(a);
    case FormulaOp::Sqrt:
        return sqrt(a);
    default:
        throw std::invalid_argument("Opcode is not unary");
    }
}

template<typename T>
T evaluateTyped(const CompiledFormula &formula, const std::unordered_map<std::string, T> &variables)
{
    std::vector<T> stack;
    stack.reserve(formula.evaluationIr.size());

    for (const auto &instruction : formula.evaluationIr)
    {
        switch (instruction.op)
        {
        case FormulaOp::PushConstant:
            stack.emplace_back(static_cast<T>(instruction.constant));
            break;
        case FormulaOp::PushVariable:
        {
            if (instruction.variableSlot >= formula.variableNames.size())
            {
                throw std::invalid_argument("Invalid variable slot in formula bytecode");
            }
            const auto &name = formula.variableNames[instruction.variableSlot];
            const auto it = variables.find(name);
            if (it == variables.end())
            {
                throw std::invalid_argument("Undefined variable: " + name);
            }
            stack.push_back(it->second);
            break;
        }
        case FormulaOp::Sin:
        case FormulaOp::Cos:
        case FormulaOp::Tan:
        case FormulaOp::Log:
        case FormulaOp::Exp:
        case FormulaOp::Sqrt:
        {
            if (stack.empty())
            {
                throw std::invalid_argument("Insufficient operands for unary formula opcode");
            }
            auto value = stack.back();
            stack.back() = applyUnary(instruction.op, value);
            break;
        }
        default:
        {
            if (stack.size() < 2)
            {
                throw std::invalid_argument("Insufficient operands for binary formula opcode");
            }
            auto rhs = stack.back();
            stack.pop_back();
            auto lhs = stack.back();
            stack.back() = applyBinary(instruction.op, lhs, rhs);
            break;
        }
        }
    }

    if (stack.size() != 1)
    {
        throw std::invalid_argument("Invalid compiled formula expression");
    }
    return stack.back();
}

template<typename T>
T evaluateTypedSlots(const CompiledFormula &formula, const std::span<const T> variablesBySlot)
{
    std::vector<T> stack;
    stack.reserve(formula.evaluationIr.size());

    for (const auto &instruction : formula.evaluationIr)
    {
        switch (instruction.op)
        {
        case FormulaOp::PushConstant:
            stack.emplace_back(static_cast<T>(instruction.constant));
            break;
        case FormulaOp::PushVariable:
        {
            if (instruction.variableSlot >= variablesBySlot.size())
            {
                throw std::invalid_argument("Undefined variable slot in formula bytecode");
            }
            stack.push_back(variablesBySlot[instruction.variableSlot]);
            break;
        }
        case FormulaOp::Sin:
        case FormulaOp::Cos:
        case FormulaOp::Tan:
        case FormulaOp::Log:
        case FormulaOp::Exp:
        case FormulaOp::Sqrt:
        {
            if (stack.empty())
            {
                throw std::invalid_argument("Insufficient operands for unary formula opcode");
            }
            auto value = stack.back();
            stack.back() = applyUnary(instruction.op, value);
            break;
        }
        default:
        {
            if (stack.size() < 2)
            {
                throw std::invalid_argument("Insufficient operands for binary formula opcode");
            }
            auto rhs = stack.back();
            stack.pop_back();
            auto lhs = stack.back();
            stack.back() = applyBinary(instruction.op, lhs, rhs);
            break;
        }
        }
    }

    if (stack.size() != 1)
    {
        throw std::invalid_argument("Invalid compiled formula expression");
    }
    return stack.back();
}

std::unique_ptr<FormulaCompiler::AstNode> cloneAst(const FormulaCompiler::AstNode &node)
{
    auto clone = std::make_unique<FormulaCompiler::AstNode>();
    clone->token = node.token;
    clone->canonical = node.canonical;
    clone->children.reserve(node.children.size());
    for (const auto &child : node.children)
    {
        clone->children.push_back(cloneAst(*child));
    }
    return clone;
}

std::unique_ptr<FormulaCompiler::AstNode> makeNumberNode(const double value)
{
    auto node = std::make_unique<FormulaCompiler::AstNode>();
    node->token = Token{TokenType::NUMBER, FormulaCompiler::normalizeNumber(std::to_string(value))};
    node->canonical = FormulaCompiler::normalizeNumber(node->token.value);
    return node;
}

std::string buildCanonical(FormulaCompiler::AstNode &node)
{
    if (node.token.type == TokenType::NUMBER)
    {
        node.canonical = FormulaCompiler::normalizeNumber(node.token.value);
        return node.canonical;
    }

    if (node.token.type == TokenType::VARIABLE)
    {
        node.canonical = node.token.value;
        return node.canonical;
    }

    for (auto &child : node.children)
    {
        buildCanonical(*child);
    }

    if (node.token.type == TokenType::FUNCTION)
    {
        node.canonical = node.token.value + "(" + node.children.front()->canonical + ")";
        return node.canonical;
    }

    if (node.token.type == TokenType::OPERATOR)
    {
        if (FormulaCompiler::isCommutative(node.token.value)
            && node.children.size() == 2
            && node.children[1]->canonical < node.children[0]->canonical)
        {
            std::swap(node.children[0], node.children[1]);
        }

        node.canonical = "(" + node.children[0]->canonical + node.token.value + node.children[1]->canonical + ")";
        return node.canonical;
    }

    throw std::invalid_argument("Unsupported AST token in canonicalization");
}

std::unique_ptr<FormulaCompiler::AstNode> simplifyIdentityComparisons(
    std::unique_ptr<FormulaCompiler::AstNode> node)
{
    for (auto &child : node->children)
    {
        child = simplifyIdentityComparisons(std::move(child));
    }

    buildCanonical(*node);

    if (node->token.type != TokenType::OPERATOR || node->children.size() != 2)
    {
        return node;
    }

    const auto &op = node->token.value;
    const auto sameOperands = node->children[0]->canonical == node->children[1]->canonical;
    if (op == "-" && sameOperands)
    {
        return makeNumberNode(0.0);
    }

    if (!sameOperands)
    {
        return node;
    }

    if (op == "=" || op == "<=" || op == ">=")
    {
        return makeNumberNode(1.0);
    }
    if (op == "!=" || op == "<" || op == ">")
    {
        return makeNumberNode(0.0);
    }

    return node;
}

std::unique_ptr<FormulaCompiler::AstNode> buildAstFromRpn(const RPN &rpn)
{
    std::vector<std::unique_ptr<FormulaCompiler::AstNode>> stack;
    stack.reserve(rpn.tokens.size());

    for (const auto &token : rpn.tokens)
    {
        if (token.type == TokenType::NUMBER || token.type == TokenType::VARIABLE)
        {
            auto node = std::make_unique<FormulaCompiler::AstNode>();
            node->token = token;
            stack.push_back(std::move(node));
            continue;
        }

        if (token.type == TokenType::FUNCTION)
        {
            if (stack.empty())
            {
                throw std::invalid_argument("Function token missing operand");
            }
            auto node = std::make_unique<FormulaCompiler::AstNode>();
            node->token = token;
            node->children.push_back(std::move(stack.back()));
            stack.pop_back();
            stack.push_back(std::move(node));
            continue;
        }

        if (token.type == TokenType::OPERATOR)
        {
            if (stack.size() < 2)
            {
                throw std::invalid_argument("Operator token missing operands");
            }
            auto rhs = std::move(stack.back());
            stack.pop_back();
            auto lhs = std::move(stack.back());
            stack.pop_back();

            auto node = std::make_unique<FormulaCompiler::AstNode>();
            node->token = token;
            node->children.push_back(std::move(lhs));
            node->children.push_back(std::move(rhs));
            stack.push_back(std::move(node));
            continue;
        }

        throw std::invalid_argument("Unsupported RPN token in formula compiler");
    }

    if (stack.size() != 1)
    {
        throw std::invalid_argument("Invalid RPN expression");
    }

    buildCanonical(*stack.back());
    auto root = simplifyIdentityComparisons(std::move(stack.back()));
    buildCanonical(*root);
    return root;
}
}

std::string FormulaCompiler::normalizeSource(const std::string_view source)
{
    std::string normalized;
    normalized.reserve(source.size());
    for (const auto c : source)
    {
        if (!std::isspace(static_cast<unsigned char>(c)))
        {
            normalized.push_back(c);
        }
    }
    return normalized;
}

std::string FormulaCompiler::normalizeNumber(const std::string_view value)
{
    const auto parsed = std::stod(std::string{value});
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << parsed;
    return out.str();
}

bool FormulaCompiler::isComparison(const std::string_view op)
{
    return op == ">" || op == "<" || op == ">=" || op == "<=" || op == "=" || op == "!=";
}

bool FormulaCompiler::isCommutative(const std::string_view op)
{
    return op == "+" || op == "*" || op == "&&" || op == "||" || op == "=" || op == "!=";
}

FormulaTraits FormulaCompiler::traitsForRoot(const AstNode &root)
{
    FormulaTraits traits;
    if (root.token.type == TokenType::OPERATOR && isComparison(root.token.value))
    {
        traits.hasComparison = true;
        traits.hasEquality = root.token.value == "=";
        traits.supportsContour = root.token.value == "="
            || root.token.value == "<"
            || root.token.value == "<="
            || root.token.value == ">"
            || root.token.value == ">=";
        traits.supportsRegion = root.token.value != "=";
    }
    return traits;
}

std::optional<FormulaOp> FormulaCompiler::opcodeForToken(const Token &token)
{
    if (token.type == TokenType::OPERATOR)
    {
        if (token.value == "+") return FormulaOp::Add;
        if (token.value == "-") return FormulaOp::Subtract;
        if (token.value == "*") return FormulaOp::Multiply;
        if (token.value == "/") return FormulaOp::Divide;
        if (token.value == "^") return FormulaOp::Power;
        if (token.value == ">") return FormulaOp::Greater;
        if (token.value == "<") return FormulaOp::Less;
        if (token.value == ">=") return FormulaOp::GreaterEqual;
        if (token.value == "<=") return FormulaOp::LessEqual;
        if (token.value == "=") return FormulaOp::Equal;
        if (token.value == "!=") return FormulaOp::NotEqual;
        if (token.value == "&&") return FormulaOp::And;
        if (token.value == "||") return FormulaOp::Or;
    }

    if (token.type == TokenType::FUNCTION)
    {
        if (token.value == "sin") return FormulaOp::Sin;
        if (token.value == "cos") return FormulaOp::Cos;
        if (token.value == "tan") return FormulaOp::Tan;
        if (token.value == "log") return FormulaOp::Log;
        if (token.value == "exp") return FormulaOp::Exp;
        if (token.value == "sqrt") return FormulaOp::Sqrt;
    }

    return std::nullopt;
}

void FormulaCompiler::emitBytecode(const AstNode &node,
                                   std::vector<FormulaInstruction> &out,
                                   std::vector<std::string> &variables)
{
    if (node.token.type == TokenType::NUMBER)
    {
        out.push_back({FormulaOp::PushConstant, std::stod(node.token.value), 0});
        return;
    }

    if (node.token.type == TokenType::VARIABLE)
    {
        auto it = std::ranges::find(variables, node.token.value);
        if (it == variables.end())
        {
            variables.push_back(node.token.value);
            it = std::prev(variables.end());
        }
        out.push_back({FormulaOp::PushVariable, 0.0, static_cast<size_t>(std::distance(variables.begin(), it))});
        return;
    }

    for (const auto &child : node.children)
    {
        emitBytecode(*child, out, variables);
    }

    const auto opcode = opcodeForToken(node.token);
    if (!opcode)
    {
        throw std::invalid_argument("Unsupported formula opcode: " + node.token.value);
    }
    out.push_back({*opcode, 0.0, 0});
}

CompiledFormula FormulaCompiler::compile(std::string expression) const
{
    CompiledFormula result;
    result.source = std::move(expression);
    result.normalizedSource = normalizeSource(result.source);

    try
    {
        Tokenizer tokenizer{result.source};
        const auto tokens = tokenizer.tokenize();
        Parser parser;
        result.legacyRpn = parser.convertToRPN(tokens);

        auto root = buildAstFromRpn(result.legacyRpn);
        result.canonicalExpression = root->canonical;
        result.handle.traits = traitsForRoot(*root);

        if (result.handle.traits.hasComparison && root->children.size() == 2)
        {
            result.residualCanonicalExpression = "("
                + root->children[0]->canonical
                + "-"
                + root->children[1]->canonical
                + ")";
        }

        emitBytecode(*root, result.evaluationIr, result.variableNames);

        result.handle.sourceHash = FormulaSourceHash{stableHash(result.normalizedSource)};
        result.handle.semanticsHash = FormulaSemanticsHash{stableHash(result.canonicalExpression)};
        result.handle.backendHash = FormulaBackendHash{
            stableHash(result.canonicalExpression + "|cpu-bytecode-v1|interval-bytecode-v1|opencl-lowering-v1")
        };
        result.handle.id = result.handle.semanticsHash.value;
        result.handle.version = 1;
        result.diagnostics = {true, {}};
    }
    catch (const std::exception &e)
    {
        result.diagnostics = {false, e.what()};
    }

    return result;
}

double CompiledFormula::evaluateDouble(const std::unordered_map<std::string, double> &variables) const
{
    return evaluateTyped(*this, variables);
}

double CompiledFormula::evaluateDouble(const std::span<const double> variablesBySlot) const
{
    return evaluateTypedSlots(*this, variablesBySlot);
}

Interval CompiledFormula::evaluateInterval(const std::unordered_map<std::string, Interval> &variables) const
{
    return evaluateTyped(*this, variables);
}

Interval CompiledFormula::evaluateInterval(const std::span<const Interval> variablesBySlot) const
{
    return evaluateTypedSlots(*this, variablesBySlot);
}

std::optional<size_t> CompiledFormula::variableSlot(const std::string_view name) const
{
    const auto it = std::ranges::find(variableNames, name);
    if (it == variableNames.end())
    {
        return std::nullopt;
    }
    return static_cast<size_t>(std::distance(variableNames.begin(), it));
}
}
