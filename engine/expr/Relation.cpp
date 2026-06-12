#include "Relation.h"

#include "Parser.h"

#include <algorithm>
#include <cmath>

namespace gxr
{
namespace
{
bool usesVar(const Node &n, int slot)
{
    if (n.kind == NodeKind::Var) return n.slot == slot;
    if (n.a && usesVar(*n.a, slot)) return true;
    if (n.b && usesVar(*n.b, slot)) return true;
    return false;
}

std::unique_ptr<Node> cloneVar(int slot)
{
    auto n = std::make_unique<Node>();
    n->kind = NodeKind::Var;
    n->slot = slot;
    return n;
}

CmpOp nodeKindToOp(NodeKind k)
{
    switch (k)
    {
    case NodeKind::Less: return CmpOp::Less;
    case NodeKind::LessEq: return CmpOp::LessEq;
    case NodeKind::Greater: return CmpOp::Greater;
    case NodeKind::GreaterEq: return CmpOp::GreaterEq;
    case NodeKind::Equal: return CmpOp::Equal;
    default: return CmpOp::NotEqual;
    }
}

CmpOp flipOp(CmpOp op) // a op b  <=>  b flip(op) a
{
    switch (op)
    {
    case CmpOp::Less: return CmpOp::Greater;
    case CmpOp::LessEq: return CmpOp::GreaterEq;
    case CmpOp::Greater: return CmpOp::Less;
    case CmpOp::GreaterEq: return CmpOp::LessEq;
    default: return op; // Equal/NotEqual symmetric
    }
}
}

std::optional<Relation> Relation::parse(const std::string &text, std::string &error,
                                        const std::unordered_map<char, double> *params,
                                        std::vector<char> *usedParams)
{
    ParseResult pr = parseExpression(text, params, usedParams);
    if (!pr.ok)
    {
        error = pr.error;
        return std::nullopt;
    }

    Relation r;
    r.source_ = text;
    Node &root = *pr.root;

    if (isComparison(root.kind))
    {
        r.single_ = true;
        r.op_ = nodeKindToOp(root.kind);

        // f = lhs - rhs
        auto fNode = std::make_unique<Node>();
        fNode->kind = NodeKind::Sub;
        fNode->a = std::move(root.a);
        fNode->b = std::move(root.b);

        // Detect explicit-1D structure BEFORE the operands are consumed by
        // compile: v <op> g(w) where v is x or y, isolated, and g uses only w.
        const Node *lhs = fNode->a.get();
        const Node *rhs = fNode->b.get();
        auto isVar = [](const Node *n, int slot) {
            return n && n->kind == NodeKind::Var && n->slot == slot;
        };
        // prefer y-explicit (y op g(x)), then x-explicit (x op g(y))
        if (isVar(lhs, 1) && !usesVar(*rhs, 1))
        {
            r.explicit1D_ = true;
            r.explicitIsY_ = true;
            r.g1d_ = Program::compile(*rhs);
            r.op1d_ = r.op_;
        }
        else if (isVar(rhs, 1) && !usesVar(*lhs, 1))
        {
            r.explicit1D_ = true;
            r.explicitIsY_ = true;
            r.g1d_ = Program::compile(*lhs);
            r.op1d_ = flipOp(r.op_);
        }
        else if (isVar(lhs, 0) && !usesVar(*rhs, 0))
        {
            r.explicit1D_ = true;
            r.explicitIsY_ = false;
            r.g1d_ = Program::compile(*rhs);
            r.op1d_ = r.op_;
        }
        else if (isVar(rhs, 0) && !usesVar(*lhs, 0))
        {
            r.explicit1D_ = true;
            r.explicitIsY_ = false;
            r.g1d_ = Program::compile(*lhs);
            r.op1d_ = flipOp(r.op_);
        }

        r.f_ = Program::compile(*fNode);
        return r;
    }

    // compound boolean or bare expression
    if (isLogical(root.kind))
    {
        r.single_ = false;
        r.f_ = Program::compile(root);
        return r;
    }

    // bare real expression: treat as `expr > 0`
    r.single_ = true;
    r.op_ = CmpOp::Greater;
    r.f_ = Program::compile(root);
    return r;
}

Interval Relation::centeredValue(const Interval &x, const Interval &y, EvalScratch &s) const
{
    const Jet j = f_.evalJet(x, y, s.sj);
    Interval naive = j.v;
    if (naive.undef) return naive;

    // The centered form assumes f is smooth on the box; only trust it when the
    // value and gradients are finite and gap-free.
    const bool smooth = !naive.disc && std::isfinite(j.dx.lo) && std::isfinite(j.dx.hi)
                        && std::isfinite(j.dy.lo) && std::isfinite(j.dy.hi);
    if (!smooth) return naive;

    const Interval cx{x.mid()};
    const Interval cy{y.mid()};
    const Interval fc = f_.evalInterval(cx, cy, s.si);
    if (fc.undef || fc.disc) return naive;

    const Interval centered = fc + j.dx * (x - cx) + j.dy * (y - cy);

    // sound intersection of the two enclosures
    Interval out;
    out.lo = std::max(naive.lo, centered.lo);
    out.hi = std::min(naive.hi, centered.hi);
    out.disc = naive.disc;
    if (out.lo > out.hi)
    {
        // numerical disagreement: fall back to the sound naive range
        return naive;
    }
    return out;
}

Sign Relation::classifyValueVsZero(const Interval &v) const
{
    if (v.undef) return Sign::Undefined;
    if (v.disc) return Sign::Uncertain; // singularity in box -> must split
    switch (op_)
    {
    case CmpOp::Less:
        if (v.hi < 0.0) return Sign::AllTrue;
        if (v.lo >= 0.0) return Sign::AllFalse;
        return Sign::Uncertain;
    case CmpOp::LessEq:
        if (v.hi <= 0.0) return Sign::AllTrue;
        if (v.lo > 0.0) return Sign::AllFalse;
        return Sign::Uncertain;
    case CmpOp::Greater:
        if (v.lo > 0.0) return Sign::AllTrue;
        if (v.hi <= 0.0) return Sign::AllFalse;
        return Sign::Uncertain;
    case CmpOp::GreaterEq:
        if (v.lo >= 0.0) return Sign::AllTrue;
        if (v.hi < 0.0) return Sign::AllFalse;
        return Sign::Uncertain;
    case CmpOp::Equal:
        if (v.lo > 0.0 || v.hi < 0.0) return Sign::AllFalse;
        return Sign::Uncertain; // never AllTrue for a positive-width box
    case CmpOp::NotEqual:
        if (v.lo > 0.0 || v.hi < 0.0) return Sign::AllTrue;
        return Sign::Uncertain;
    }
    return Sign::Uncertain;
}

Sign Relation::classifyNaive(const Interval &x, const Interval &y, EvalScratch &s) const
{
    if (single_)
    {
        return classifyValueVsZero(f_.evalInterval(x, y, s.si));
    }
    // general boolean truth program
    const Interval t = f_.evalInterval(x, y, s.si);
    if (t.undef) return Sign::Undefined;
    if (t.disc) return Sign::Uncertain;
    if (t.lo >= 1.0) return Sign::AllTrue;
    if (t.hi <= 0.0) return Sign::AllFalse;
    return Sign::Uncertain;
}

Sign Relation::classifyRefined(const Interval &x, const Interval &y, EvalScratch &s) const
{
    if (single_)
    {
        return classifyValueVsZero(centeredValue(x, y, s));
    }
    return classifyNaive(x, y, s); // no centered form for compound boolean truth
}

Sign Relation::classifyBox(const Interval &x, const Interval &y, EvalScratch &s) const
{
    const Sign sn = classifyNaive(x, y, s);
    if (sn != Sign::Uncertain || !single_) return sn;
    return classifyRefined(x, y, s);
}

bool Relation::pointInside(double x, double y, EvalScratch &s) const
{
    if (single_)
    {
        const double f = f_.evalPoint(x, y, s.sd);
        if (!std::isfinite(f))
        {
            // undefined point: treat equality as off, inequalities as off
            return false;
        }
        switch (op_)
        {
        case CmpOp::Less: return f < 0.0;
        case CmpOp::LessEq: return f <= 0.0;
        case CmpOp::Greater: return f > 0.0;
        case CmpOp::GreaterEq: return f >= 0.0;
        case CmpOp::Equal: return f == 0.0;
        case CmpOp::NotEqual: return f != 0.0;
        }
        return false;
    }
    const double t = f_.evalPoint(x, y, s.sd);
    return std::isfinite(t) && t != 0.0;
}

int Relation::pointInsideCount(const double *xs, const double *ys, int n, EvalScratch &s) const
{
    int hits = 0;
    double vals[256];
    for (int base = 0; base < n; base += 256)
    {
        const int w = std::min(n - base, 256);
        f_.evalPointBatch(xs + base, ys + base, vals, w, s.sb);
        if (single_)
        {
            switch (op_)
            {
            case CmpOp::Less:
                for (int i = 0; i < w; ++i) hits += std::isfinite(vals[i]) && vals[i] < 0.0;
                break;
            case CmpOp::LessEq:
                for (int i = 0; i < w; ++i) hits += std::isfinite(vals[i]) && vals[i] <= 0.0;
                break;
            case CmpOp::Greater:
                for (int i = 0; i < w; ++i) hits += std::isfinite(vals[i]) && vals[i] > 0.0;
                break;
            case CmpOp::GreaterEq:
                for (int i = 0; i < w; ++i) hits += std::isfinite(vals[i]) && vals[i] >= 0.0;
                break;
            case CmpOp::Equal:
                for (int i = 0; i < w; ++i) hits += vals[i] == 0.0;
                break;
            case CmpOp::NotEqual:
                for (int i = 0; i < w; ++i) hits += std::isfinite(vals[i]) && vals[i] != 0.0;
                break;
            }
        }
        else
        {
            for (int i = 0; i < w; ++i) hits += std::isfinite(vals[i]) && vals[i] != 0.0;
        }
    }
    return hits;
}

void Relation::pointInsideMask(const double *xs, const double *ys, int n, unsigned char *inside,
                               EvalScratch &s) const
{
    double vals[256];
    for (int base = 0; base < n; base += 256)
    {
        const int w = std::min(n - base, 256);
        f_.evalPointBatch(xs + base, ys + base, vals, w, s.sb);
        unsigned char *out = inside + base;
        if (single_)
        {
            switch (op_)
            {
            case CmpOp::Less:
                for (int i = 0; i < w; ++i) out[i] = std::isfinite(vals[i]) && vals[i] < 0.0;
                break;
            case CmpOp::LessEq:
                for (int i = 0; i < w; ++i) out[i] = std::isfinite(vals[i]) && vals[i] <= 0.0;
                break;
            case CmpOp::Greater:
                for (int i = 0; i < w; ++i) out[i] = std::isfinite(vals[i]) && vals[i] > 0.0;
                break;
            case CmpOp::GreaterEq:
                for (int i = 0; i < w; ++i) out[i] = std::isfinite(vals[i]) && vals[i] >= 0.0;
                break;
            case CmpOp::Equal:
                for (int i = 0; i < w; ++i) out[i] = vals[i] == 0.0;
                break;
            case CmpOp::NotEqual:
                for (int i = 0; i < w; ++i) out[i] = std::isfinite(vals[i]) && vals[i] != 0.0;
                break;
            }
        }
        else
        {
            for (int i = 0; i < w; ++i) out[i] = std::isfinite(vals[i]) && vals[i] != 0.0;
        }
    }
}

void Relation::valueAndGrad(const Interval &x, const Interval &y, EvalScratch &s, Interval &val,
                            Interval &gx, Interval &gy) const
{
    const Jet j = f_.evalJet(x, y, s.sj);
    val = centeredValue(x, y, s);
    gx = j.dx;
    gy = j.dy;
}
}
