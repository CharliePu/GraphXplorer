#ifndef GXR_EXPR_RELATION_H
#define GXR_EXPR_RELATION_H

#include <unordered_map>
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

// Per-thread reusable scratch for the evaluation modes (no per-call alloc).
struct EvalScratch
{
    std::vector<double> sd;
    std::vector<Interval> si;
    std::vector<Jet> sj;
    std::vector<double> sb; // batch-evaluation slab (evalPointBatch)
};

// A compiled implicit relation over (x, y). Two strategies:
//   * SingleCompare:  f(x,y) `op` 0  -> centered-form classification + gradient.
//   * GeneralBool:    a truth-valued program (compound &&/|| of comparisons).
class Relation
{
public:
    [[nodiscard]] static std::optional<Relation> parse(
        const std::string &text, std::string &error,
        const std::unordered_map<char, double> *params = nullptr,
        std::vector<char> *usedParams = nullptr);

    [[nodiscard]] bool isEquality() const { return single_ && (op_ == CmpOp::Equal || op_ == CmpOp::NotEqual); }
    [[nodiscard]] bool isNotEqual() const { return single_ && op_ == CmpOp::NotEqual; }
    // closed inequality (>=, <=): the boundary belongs to the set and renders
    // as a line; strict (<, >) excludes it and renders fill only
    [[nodiscard]] bool isClosedInequality() const
    {
        return single_ && (op_ == CmpOp::LessEq || op_ == CmpOp::GreaterEq);
    }
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

    // Two-stage classification for the adaptive solver: the cheap naive interval
    // first, then (only if still uncertain and worth it) the costlier centered
    // form. The solver disables the refined stage per-tile when it stops helping.
    [[nodiscard]] Sign classifyNaive(const Interval &x, const Interval &y, EvalScratch &s) const;
    [[nodiscard]] Sign classifyRefined(const Interval &x, const Interval &y, EvalScratch &s) const;

    // Point membership (used for unbiased sub-pixel sampling at the floor).
    [[nodiscard]] bool pointInside(double x, double y, EvalScratch &s) const;

    // Batched membership for the sampling paths: how many of the n points
    // satisfy the relation (pointInside semantics, SIMD-evaluated).
    [[nodiscard]] int pointInsideCount(const double *xs, const double *ys, int n,
                                       EvalScratch &s) const;
    // Per-point membership flags, for batches whose samples belong to
    // different cells (the solver's deferred floor-cell buffer).
    void pointInsideMask(const double *xs, const double *ys, int n, unsigned char *inside,
                         EvalScratch &s) const;

    // Value range and gradient of f over a box (for the equality band model).
    void valueAndGrad(const Interval &x, const Interval &y, EvalScratch &s, Interval &val,
                      Interval &gx, Interval &gy) const;

    // structure: explicit-1D means the relation is `v <op> g(w)` where v is one
    // axis variable (isolated, coefficient 1) and g depends only on the other.
    [[nodiscard]] bool explicit1D() const { return explicit1D_; }
    [[nodiscard]] bool explicitIsY() const { return explicitIsY_; } // y<op>g(x) vs x<op>g(y)
    [[nodiscard]] const Program *explicitG() const { return explicit1D_ ? &g1d_ : nullptr; }
    [[nodiscard]] CmpOp explicitOp() const { return op1d_; }

    [[nodiscard]] const std::string &source() const { return source_; }

private:
    std::string source_;
    bool single_{true};
    CmpOp op_{CmpOp::Less};
    Program f_;       // for SingleCompare: f(x,y); for GeneralBool: truth program
    bool explicit1D_{false};
    bool explicitIsY_{true};
    Program g1d_; // g(.) for explicit relations
    CmpOp op1d_{CmpOp::Less};

    [[nodiscard]] Interval centeredValue(const Interval &x, const Interval &y, EvalScratch &s) const;
    [[nodiscard]] Sign classifyValueVsZero(const Interval &v) const;
};
}

#endif // GXR_EXPR_RELATION_H
