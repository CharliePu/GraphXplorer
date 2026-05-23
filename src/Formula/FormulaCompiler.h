#ifndef FORMULACOMPILER_H
#define FORMULACOMPILER_H

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../Math/Interval.h"
#include "../Util/Contracts.h"
#include "Parser.h"

namespace gx
{
enum class FormulaOp
{
    PushConstant,
    PushVariable,
    Add,
    Subtract,
    Multiply,
    Divide,
    Power,
    Greater,
    Less,
    GreaterEqual,
    LessEqual,
    Equal,
    NotEqual,
    And,
    Or,
    Sin,
    Cos,
    Tan,
    Log,
    Exp,
    Sqrt
};

struct FormulaInstruction
{
    FormulaOp op{FormulaOp::PushConstant};
    double constant{0.0};
    size_t variableSlot{0};
    bool operator==(const FormulaInstruction &) const = default;
};

struct FormulaDiagnostics
{
    bool ok{true};
    std::string message{};
};

struct AffineInequality
{
    double xCoefficient{0.0};
    double yCoefficient{0.0};
    double constant{0.0};
    FormulaOp comparison{FormulaOp::LessEqual};
    bool operator==(const AffineInequality &) const = default;
};

struct TangentPoleInequality
{
    double argumentXCoefficient{0.0};
    double argumentYCoefficient{0.0};
    double argumentConstant{0.0};
    FormulaOp comparison{FormulaOp::Greater};
    bool operator==(const TangentPoleInequality &) const = default;
};

struct FormulaBytecodeSlice
{
    size_t offset{0};
    size_t count{0};
    uint64_t variableMask{0};
    bool operator==(const FormulaBytecodeSlice &) const = default;
};

struct RootComparisonBytecode
{
    FormulaOp comparison{FormulaOp::LessEqual};
    FormulaBytecodeSlice lhs{};
    FormulaBytecodeSlice rhs{};
    bool operator==(const RootComparisonBytecode &) const = default;
};

struct CompiledFormula
{
    CompiledFormulaHandle handle{};
    std::string source{};
    std::string normalizedSource{};
    std::string canonicalExpression{};
    std::optional<std::string> residualCanonicalExpression{};
    std::optional<AffineInequality> affineInequality{};
    std::optional<TangentPoleInequality> tangentPoleInequality{};
    std::optional<RootComparisonBytecode> rootComparisonBytecode{};
    std::vector<std::string> variableNames{};
    std::vector<FormulaInstruction> evaluationIr{};
    RPN legacyRpn{};
    FormulaDiagnostics diagnostics{};

    [[nodiscard]] double evaluateDouble(const std::unordered_map<std::string, double> &variables) const;
    [[nodiscard]] double evaluateDouble(std::span<const double> variablesBySlot) const;
    [[nodiscard]] double evaluateDouble(std::span<const double> variablesBySlot,
                                        std::vector<double> &stackScratch) const;
    [[nodiscard]] double evaluateDoubleBytecode(const FormulaBytecodeSlice &slice,
                                                std::span<const double> variablesBySlot,
                                                std::vector<double> &stackScratch) const;
    [[nodiscard]] Interval evaluateInterval(const std::unordered_map<std::string, Interval> &variables) const;
    [[nodiscard]] Interval evaluateInterval(std::span<const Interval> variablesBySlot) const;
    [[nodiscard]] Interval evaluateInterval(std::span<const Interval> variablesBySlot,
                                            std::vector<Interval> &stackScratch) const;
    [[nodiscard]] Interval evaluateIntervalBytecode(const FormulaBytecodeSlice &slice,
                                                    std::span<const Interval> variablesBySlot,
                                                    std::vector<Interval> &stackScratch) const;
    [[nodiscard]] std::optional<size_t> variableSlot(std::string_view name) const;
};

class FormulaCompiler
{
public:
    struct AstNode;

    [[nodiscard]] CompiledFormula compile(std::string expression) const;
    [[nodiscard]] static std::string normalizeNumber(std::string_view value);
    [[nodiscard]] static bool isCommutative(std::string_view op);

private:
    [[nodiscard]] static std::string normalizeSource(std::string_view source);
    [[nodiscard]] static FormulaTraits traitsForRoot(const AstNode &root);
    [[nodiscard]] static bool isComparison(std::string_view op);
    [[nodiscard]] static std::optional<FormulaOp> opcodeForToken(const Token &token);

    static void emitBytecode(const AstNode &node, std::vector<FormulaInstruction> &out,
                             std::vector<std::string> &variables);
};
}

#endif // FORMULACOMPILER_H
