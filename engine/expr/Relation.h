#ifndef GXR_EXPR_RELATION_H
#define GXR_EXPR_RELATION_H

#include "Program.h"
#include "math/Interval.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gxr
{
enum class Sign
{
    AllTrue,   // relation holds over the whole box
    AllFalse,  // relation fails over the whole box
    Undefined, // relation is undefined over the whole box (contributes no area)
    Uncertain, // boundary (or a discontinuity) crosses the box
};

enum class CmpOp
{
    Less,
    LessEq,
    Greater,
    GreaterEq,
    Equal,
    NotEqual,
};

// Per-thread reusable scratch for the three evaluation modes (no per-call alloc).
struct EvalScratch
{
    std::vector<double> sd;
    std::vector<Interval> si;
    std::vector<Jet> sj;
};

// A compiled implicit relation over (x, y). Two strategies:
//   * SingleCompare:  f(x,y) `op` 0  -> centered-form classification + gradient.
//   * GeneralBool:    a truth-valued program (compound &&/|| of comparisons).
class Relation
{
public:
    [[nodiscard]] static std::optional<Relation> parse(const std::string &text, std::string &error);

    [[nodiscard]] bool isEquality() const { return single_ && (op_ == CmpOp::Equal || op_ == CmpOp::NotEqual); }
    [[nodiscard]] bool isNotEqual() const { return single_ && op_ == CmpOp::NotEqual; }
    [[nodiscard]] bool isSingleCompare() const { return single_; }
    [[nodiscard]] CmpOp op() const { return op_; }

    // Point value of f (single-compare only); NaN where undefined.
    [[nodiscard]] double fValue(double x, double y, EvalScratch &s) const
    {
        return f_.evalPoint(x, y, s.sd);
    }

    // Classify a world box [x]x[y]. Uses the centered (mean-value) form to tighten
    // the value range wherever f is smooth on the box.
    [[nodiscard]] Sign classifyBox(const Interval &x, const Interval &y, EvalScratch &s) const;

    // Point membership (used for unbiased sub-pixel sampling at the floor).
    [[nodiscard]] bool pointInside(double x, double y, EvalScratch &s) const;

    // Value range and gradient of f over a box (for the equality band model).
    void valueAndGrad(const Interval &x, const Interval &y, EvalScratch &s, Interval &val,
                      Interval &gx, Interval &gy) const;

    // structure
    [[nodiscard]] bool explicitY() const { return explicitY_; }
    [[nodiscard]] const Program *explicitG() const { return explicitY_ ? &gProgram_ : nullptr; }
    // normalized op for "y <opY> g(x)"
    [[nodiscard]] CmpOp explicitOpY() const { return opY_; }

    [[nodiscard]] const std::string &source() const { return source_; }

private:
    std::string source_;
    bool single_{true};
    CmpOp op_{CmpOp::Less};
    Program f_;       // for SingleCompare: f(x,y); for GeneralBool: truth program
    bool explicitY_{false};
    Program gProgram_; // g(x) for explicit y relations
    CmpOp opY_{CmpOp::Less};

    [[nodiscard]] Interval centeredValue(const Interval &x, const Interval &y, EvalScratch &s) const;
    [[nodiscard]] Sign classifyValueVsZero(const Interval &v) const;
};
}

#endif // GXR_EXPR_RELATION_H
