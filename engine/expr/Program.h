#ifndef GXR_EXPR_PROGRAM_H
#define GXR_EXPR_PROGRAM_H

#include "Ast.h"
#include "math/Interval.h"

#include <cstdint>
#include <vector>

namespace gxr
{
enum class Op : uint8_t
{
    PushConst,
    PushVar,
    Neg,
    Add,
    Sub,
    Mul,
    Div,
    IPow,  // integer constant exponent in imm
    Pow,   // general
    Abs,
    Sin,
    Cos,
    Tan,
    Asin,
    Acos,
    Atan,
    Log,
    Exp,
    Sqrt,
    Less,
    LessEq,
    Greater,
    GreaterEq,
    Equal,
    NotEqual,
    And,
    Or,
};

struct Instr
{
    Op op{Op::PushConst};
    double imm{0.0};
    int slot{0};
};

// Forward-mode AD jet over intervals: value and partials wrt x (slot 0) and y.
struct Jet
{
    Interval v;
    Interval dx;
    Interval dy;
};

// Flat stack bytecode. Evaluable as a point (double), a naive interval, or an
// interval jet. Truth-valued ops (comparisons/logic) are only meaningful in the
// double/interval modes; the real-valued `f` side of a relation is what gets
// jet-evaluated.
class Program
{
public:
    std::vector<Instr> code;
    bool hasTruthOps{false}; // contains comparison/logic ops
    bool usesY{false};
    int maxStack{0};

    [[nodiscard]] static Program compile(const Node &root);

    [[nodiscard]] double evalPoint(double x, double y, std::vector<double> &stack) const;
    [[nodiscard]] Interval evalInterval(const Interval &x, const Interval &y,
                                        std::vector<Interval> &stack) const;
    [[nodiscard]] Jet evalJet(const Interval &x, const Interval &y, std::vector<Jet> &stack) const;
};
}

#endif // GXR_EXPR_PROGRAM_H
