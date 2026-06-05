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

std::optional<Relation> Relation::parse(const std::string &text, std::string &error)
{
    ParseResult pr = parseExpression(text);
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
        // detect explicit-y BEFORE the operands are consumed by compile
        const bool lhsIsY = fNode->a && fNode->a->kind == NodeKind::Var && fNode->a->slot == 1;
        const bool rhsIsY = fNode->b && fNode->b->kind == NodeKind::Var && fNode->b->slot == 1;
        if (lhsIsY && fNode->b && !usesVar(*fNode->b, 1))
        {
            r.explicitY_ = true;
            r.gProgram_ = Program::compile(*fNode->b); // g(x) = rhs
            r.opY_ = r.op_;                            // y op g
        }
        else if (rhsIsY && fNode->a && !usesVar(*fNode->a, 1))
        {
            r.explicitY_ = true;
            r.gProgram_ = Program::compile(*fNode->a); // g(x) = lhs
            r.opY_ = flipOp(r.op_);                    // g op y  <=>  y flip(op) g
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

Sign Relation::classifyBox(const Interval &x, const Interval &y, EvalScratch &s) const
{
    if (single_)
    {
        // Cheap naive interval first; only pay for the jet-based centered form
        // on boxes the naive range leaves uncertain (the boundary band).
        const Interval naive = f_.evalInterval(x, y, s.si);
        const Sign sn = classifyValueVsZero(naive);
        if (sn != Sign::Uncertain) return sn;
        return classifyValueVsZero(centeredValue(x, y, s));
    }

    // general boolean truth program
    const Interval t = f_.evalInterval(x, y, s.si);
    if (t.undef) return Sign::Undefined;
    if (t.disc) return Sign::Uncertain;
    if (t.lo >= 1.0) return Sign::AllTrue;
    if (t.hi <= 0.0) return Sign::AllFalse;
    return Sign::Uncertain;
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

void Relation::valueAndGrad(const Interval &x, const Interval &y, EvalScratch &s, Interval &val,
                            Interval &gx, Interval &gy) const
{
    const Jet j = f_.evalJet(x, y, s.sj);
    val = centeredValue(x, y, s);
    gx = j.dx;
    gy = j.dy;
}
}
