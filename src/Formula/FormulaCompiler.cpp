#include "FormulaCompiler.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <type_traits>
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
[[nodiscard]] double integerPower(const double base, const long long exponent)
{
    if (exponent == 0)
    {
        return 1.0;
    }

    auto remaining = exponent < 0 ? -exponent : exponent;
    auto factor = base;
    auto result = 1.0;
    while (remaining > 0)
    {
        if ((remaining & 1LL) != 0LL)
        {
            result *= factor;
        }
        remaining >>= 1;
        if (remaining > 0)
        {
            factor *= factor;
        }
    }

    return exponent < 0 ? 1.0 / result : result;
}

[[nodiscard]] double powerDouble(const double base, const double exponent)
{
    if (std::isfinite(exponent) && std::floor(exponent) == exponent)
    {
        constexpr auto maxSpecializedExponent = 64.0;
        if (exponent >= -maxSpecializedExponent && exponent <= maxSpecializedExponent)
        {
            return integerPower(base, static_cast<long long>(exponent));
        }
    }
    return std::pow(base, exponent);
}

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
        if constexpr (std::is_same_v<T, double>)
        {
            return powerDouble(a, b);
        }
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
T evaluateTypedInstructions(const std::span<const FormulaInstruction> instructions,
                            const std::span<const T> variablesBySlot,
                            std::vector<T> &stack)
{
    if (stack.size() < instructions.size())
    {
        stack.resize(instructions.size());
    }

    auto depth = size_t{0};
    for (const auto &instruction : instructions)
    {
        switch (instruction.op)
        {
        case FormulaOp::PushConstant:
            stack[depth++] = static_cast<T>(instruction.constant);
            break;
        case FormulaOp::PushVariable:
        {
            if (instruction.variableSlot >= variablesBySlot.size())
            {
                throw std::invalid_argument("Undefined variable slot in formula bytecode");
            }
            stack[depth++] = variablesBySlot[instruction.variableSlot];
            break;
        }
        case FormulaOp::Sin:
        case FormulaOp::Cos:
        case FormulaOp::Tan:
        case FormulaOp::Log:
        case FormulaOp::Exp:
        case FormulaOp::Sqrt:
        {
            if (depth == 0)
            {
                throw std::invalid_argument("Insufficient operands for unary formula opcode");
            }
            const auto value = stack[depth - 1];
            stack[depth - 1] = applyUnary(instruction.op, value);
            break;
        }
        default:
        {
            if (depth < 2)
            {
                throw std::invalid_argument("Insufficient operands for binary formula opcode");
            }
            const auto rhs = stack[--depth];
            const auto lhs = stack[depth - 1];
            stack[depth - 1] = applyBinary(instruction.op, lhs, rhs);
            break;
        }
        }
    }

    if (depth != 1)
    {
        throw std::invalid_argument("Invalid compiled formula expression");
    }
    return stack[0];
}

template<typename T>
T evaluateTypedSlots(const CompiledFormula &formula, const std::span<const T> variablesBySlot)
{
    std::vector<T> stack;
    return evaluateTypedInstructions(std::span{formula.evaluationIr}, variablesBySlot, stack);
}

template<typename T>
T evaluateTypedSlots(const CompiledFormula &formula,
                     const std::span<const T> variablesBySlot,
                     std::vector<T> &stack)
{
    return evaluateTypedInstructions(std::span{formula.evaluationIr}, variablesBySlot, stack);
}

[[nodiscard]] uint64_t variableMaskForInstructions(const std::span<const FormulaInstruction> instructions)
{
    auto mask = uint64_t{0};
    for (const auto &instruction : instructions)
    {
        if (instruction.op != FormulaOp::PushVariable)
        {
            continue;
        }
        if (instruction.variableSlot >= std::numeric_limits<uint64_t>::digits)
        {
            return std::numeric_limits<uint64_t>::max();
        }
        mask |= uint64_t{1} << instruction.variableSlot;
    }
    return mask;
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

struct AffineExpression
{
    double xCoefficient{0.0};
    double yCoefficient{0.0};
    double constant{0.0};
};

[[nodiscard]] bool isConstant(const AffineExpression &expression)
{
    return expression.xCoefficient == 0.0 && expression.yCoefficient == 0.0;
}

[[nodiscard]] AffineExpression add(const AffineExpression &lhs, const AffineExpression &rhs)
{
    return {
        .xCoefficient = lhs.xCoefficient + rhs.xCoefficient,
        .yCoefficient = lhs.yCoefficient + rhs.yCoefficient,
        .constant = lhs.constant + rhs.constant
    };
}

[[nodiscard]] AffineExpression subtract(const AffineExpression &lhs, const AffineExpression &rhs)
{
    return {
        .xCoefficient = lhs.xCoefficient - rhs.xCoefficient,
        .yCoefficient = lhs.yCoefficient - rhs.yCoefficient,
        .constant = lhs.constant - rhs.constant
    };
}

[[nodiscard]] AffineExpression scale(const AffineExpression &expression, const double factor)
{
    return {
        .xCoefficient = expression.xCoefficient * factor,
        .yCoefficient = expression.yCoefficient * factor,
        .constant = expression.constant * factor
    };
}

[[nodiscard]] std::optional<AffineExpression> affineExpressionFor(const FormulaCompiler::AstNode &node)
{
    if (node.token.type == TokenType::NUMBER)
    {
        return AffineExpression{.constant = std::stod(node.token.value)};
    }
    if (node.token.type == TokenType::VARIABLE)
    {
        if (node.token.value == "x")
        {
            return AffineExpression{.xCoefficient = 1.0};
        }
        if (node.token.value == "y")
        {
            return AffineExpression{.yCoefficient = 1.0};
        }
        return std::nullopt;
    }
    if (node.token.type != TokenType::OPERATOR || node.children.size() != 2)
    {
        return std::nullopt;
    }

    const auto lhs = affineExpressionFor(*node.children[0]);
    const auto rhs = affineExpressionFor(*node.children[1]);
    if (!lhs || !rhs)
    {
        return std::nullopt;
    }

    const auto &op = node.token.value;
    if (op == "+")
    {
        return add(*lhs, *rhs);
    }
    if (op == "-")
    {
        return subtract(*lhs, *rhs);
    }
    if (op == "*")
    {
        if (isConstant(*lhs))
        {
            return scale(*rhs, lhs->constant);
        }
        if (isConstant(*rhs))
        {
            return scale(*lhs, rhs->constant);
        }
        return std::nullopt;
    }
    if (op == "/")
    {
        if (!isConstant(*rhs) || rhs->constant == 0.0)
        {
            return std::nullopt;
        }
        return scale(*lhs, 1.0 / rhs->constant);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<FormulaOp> comparisonOpFor(const std::string_view op)
{
    if (op == ">") return FormulaOp::Greater;
    if (op == "<") return FormulaOp::Less;
    if (op == ">=") return FormulaOp::GreaterEqual;
    if (op == "<=") return FormulaOp::LessEqual;
    if (op == "=") return FormulaOp::Equal;
    if (op == "!=") return FormulaOp::NotEqual;
    return std::nullopt;
}

[[nodiscard]] std::optional<FormulaOp> flipComparison(const FormulaOp op)
{
    switch (op)
    {
    case FormulaOp::Greater:
        return FormulaOp::Less;
    case FormulaOp::Less:
        return FormulaOp::Greater;
    case FormulaOp::GreaterEqual:
        return FormulaOp::LessEqual;
    case FormulaOp::LessEqual:
        return FormulaOp::GreaterEqual;
    case FormulaOp::Equal:
    case FormulaOp::NotEqual:
        return op;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] bool tangentPoleCanProveExistence(const FormulaOp comparison)
{
    return comparison == FormulaOp::Greater
        || comparison == FormulaOp::Less
        || comparison == FormulaOp::GreaterEqual
        || comparison == FormulaOp::LessEqual
        || comparison == FormulaOp::NotEqual;
}

[[nodiscard]] std::optional<AffineInequality> affineInequalityForRoot(const FormulaCompiler::AstNode &root)
{
    if (root.token.type != TokenType::OPERATOR || root.children.size() != 2)
    {
        return std::nullopt;
    }
    const auto comparison = comparisonOpFor(root.token.value);
    if (!comparison)
    {
        return std::nullopt;
    }

    const auto lhs = affineExpressionFor(*root.children[0]);
    const auto rhs = affineExpressionFor(*root.children[1]);
    if (!lhs || !rhs)
    {
        return std::nullopt;
    }

    const auto residual = subtract(*lhs, *rhs);
    return AffineInequality{
        .xCoefficient = residual.xCoefficient,
        .yCoefficient = residual.yCoefficient,
        .constant = residual.constant,
        .comparison = *comparison
    };
}

[[nodiscard]] std::optional<AffineExpression> tanArgumentExpressionFor(const FormulaCompiler::AstNode &node)
{
    if (node.token.type != TokenType::FUNCTION || node.token.value != "tan" || node.children.size() != 1)
    {
        return std::nullopt;
    }
    return affineExpressionFor(*node.children.front());
}

[[nodiscard]] std::optional<TangentPoleInequality> tangentPoleInequalityForRoot(
    const FormulaCompiler::AstNode &root)
{
    if (root.token.type != TokenType::OPERATOR || root.children.size() != 2)
    {
        return std::nullopt;
    }

    const auto comparison = comparisonOpFor(root.token.value);
    if (!comparison)
    {
        return std::nullopt;
    }

    if (const auto lhsArgument = tanArgumentExpressionFor(*root.children[0]))
    {
        if (!tangentPoleCanProveExistence(*comparison) || !affineExpressionFor(*root.children[1]))
        {
            return std::nullopt;
        }
        return TangentPoleInequality{
            .argumentXCoefficient = lhsArgument->xCoefficient,
            .argumentYCoefficient = lhsArgument->yCoefficient,
            .argumentConstant = lhsArgument->constant,
            .comparison = *comparison
        };
    }

    if (const auto rhsArgument = tanArgumentExpressionFor(*root.children[1]))
    {
        const auto flipped = flipComparison(*comparison);
        if (!flipped || !tangentPoleCanProveExistence(*flipped) || !affineExpressionFor(*root.children[0]))
        {
            return std::nullopt;
        }
        return TangentPoleInequality{
            .argumentXCoefficient = rhsArgument->xCoefficient,
            .argumentYCoefficient = rhsArgument->yCoefficient,
            .argumentConstant = rhsArgument->constant,
            .comparison = *flipped
        };
    }

    return std::nullopt;
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
            result.affineInequality = affineInequalityForRoot(*root);
            result.tangentPoleInequality = tangentPoleInequalityForRoot(*root);
        }

        const auto rootOpcode = opcodeForToken(root->token);
        if (result.handle.traits.hasComparison && root->children.size() == 2 && rootOpcode)
        {
            const auto lhsOffset = result.evaluationIr.size();
            emitBytecode(*root->children[0], result.evaluationIr, result.variableNames);
            const auto lhsCount = result.evaluationIr.size() - lhsOffset;

            const auto rhsOffset = result.evaluationIr.size();
            emitBytecode(*root->children[1], result.evaluationIr, result.variableNames);
            const auto rhsCount = result.evaluationIr.size() - rhsOffset;

            result.evaluationIr.push_back({*rootOpcode, 0.0, 0});
            result.rootComparisonBytecode = RootComparisonBytecode{
                .comparison = *rootOpcode,
                .lhs = FormulaBytecodeSlice{
                    .offset = lhsOffset,
                    .count = lhsCount,
                    .variableMask = variableMaskForInstructions(std::span{result.evaluationIr}.subspan(
                        lhsOffset,
                        lhsCount))
                },
                .rhs = FormulaBytecodeSlice{
                    .offset = rhsOffset,
                    .count = rhsCount,
                    .variableMask = variableMaskForInstructions(std::span{result.evaluationIr}.subspan(
                        rhsOffset,
                        rhsCount))
                }
            };
        }
        else
        {
            emitBytecode(*root, result.evaluationIr, result.variableNames);
        }

        result.handle.sourceHash = FormulaSourceHash{stableHash(result.normalizedSource)};
        result.handle.semanticsHash = FormulaSemanticsHash{stableHash(result.canonicalExpression)};
        result.handle.backendHash = FormulaBackendHash{
            stableHash(result.canonicalExpression + "|cpu-bytecode-v2|interval-bytecode-v2|opencl-lowering-v1")
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

double CompiledFormula::evaluateDouble(const std::span<const double> variablesBySlot,
                                       std::vector<double> &stackScratch) const
{
    return evaluateTypedSlots(*this, variablesBySlot, stackScratch);
}

double CompiledFormula::evaluateDoubleBytecode(const FormulaBytecodeSlice &slice,
                                               const std::span<const double> variablesBySlot,
                                               std::vector<double> &stackScratch) const
{
    if (slice.offset > evaluationIr.size() || slice.count > evaluationIr.size() - slice.offset)
    {
        throw std::invalid_argument("Formula bytecode slice is out of range");
    }
    return evaluateTypedInstructions(
        std::span{evaluationIr}.subspan(slice.offset, slice.count),
        variablesBySlot,
        stackScratch);
}

Interval CompiledFormula::evaluateInterval(const std::unordered_map<std::string, Interval> &variables) const
{
    return evaluateTyped(*this, variables);
}

Interval CompiledFormula::evaluateInterval(const std::span<const Interval> variablesBySlot) const
{
    return evaluateTypedSlots(*this, variablesBySlot);
}

Interval CompiledFormula::evaluateInterval(const std::span<const Interval> variablesBySlot,
                                           std::vector<Interval> &stackScratch) const
{
    return evaluateTypedSlots(*this, variablesBySlot, stackScratch);
}

Interval CompiledFormula::evaluateIntervalBytecode(const FormulaBytecodeSlice &slice,
                                                   const std::span<const Interval> variablesBySlot,
                                                   std::vector<Interval> &stackScratch) const
{
    if (slice.offset > evaluationIr.size() || slice.count > evaluationIr.size() - slice.offset)
    {
        throw std::invalid_argument("Formula bytecode slice is out of range");
    }
    return evaluateTypedInstructions(
        std::span{evaluationIr}.subspan(slice.offset, slice.count),
        variablesBySlot,
        stackScratch);
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
