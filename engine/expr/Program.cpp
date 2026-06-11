#include "Program.h"

#include "math/Round.h"
#include "math/Simd.h"

#include <algorithm>
#include <cmath>

namespace gxr
{
namespace
{
bool isIntConst(const Node &n, long long &out)
{
    if (n.kind == NodeKind::Const && std::floor(n.value) == n.value && std::abs(n.value) < 1e9)
    {
        out = static_cast<long long>(n.value);
        return true;
    }
    return false;
}

void emit(const Node &n, Program &p)
{
    switch (n.kind)
    {
    case NodeKind::Const:
        p.code.push_back({Op::PushConst, n.value, 0});
        return;
    case NodeKind::Var:
        if (n.slot == 1) p.usesY = true;
        p.code.push_back({Op::PushVar, 0.0, n.slot});
        return;
    case NodeKind::Neg:
        emit(*n.a, p);
        p.code.push_back({Op::Neg, 0.0, 0});
        return;
    case NodeKind::Pow:
    {
        long long e;
        if (n.b && isIntConst(*n.b, e))
        {
            emit(*n.a, p);
            p.code.push_back({Op::IPow, static_cast<double>(e), 0});
            return;
        }
        emit(*n.a, p);
        emit(*n.b, p);
        p.code.push_back({Op::Pow, 0.0, 0});
        return;
    }
    default:
        break;
    }

    // unary functions
    auto unary = [&](Op op) {
        emit(*n.a, p);
        p.code.push_back({op, 0.0, 0});
    };
    auto binary = [&](Op op) {
        emit(*n.a, p);
        emit(*n.b, p);
        p.code.push_back({op, 0.0, 0});
    };

    switch (n.kind)
    {
    case NodeKind::Abs: unary(Op::Abs); return;
    case NodeKind::Sin: unary(Op::Sin); return;
    case NodeKind::Cos: unary(Op::Cos); return;
    case NodeKind::Tan: unary(Op::Tan); return;
    case NodeKind::Asin: unary(Op::Asin); return;
    case NodeKind::Acos: unary(Op::Acos); return;
    case NodeKind::Atan: unary(Op::Atan); return;
    case NodeKind::Log: unary(Op::Log); return;
    case NodeKind::Exp: unary(Op::Exp); return;
    case NodeKind::Sqrt: unary(Op::Sqrt); return;
    case NodeKind::Floor: unary(Op::Floor); return;
    case NodeKind::Ceil: unary(Op::Ceil); return;
    case NodeKind::Sign: unary(Op::Sign); return;
    case NodeKind::Min: binary(Op::Min); return;
    case NodeKind::Max: binary(Op::Max); return;
    case NodeKind::Add: binary(Op::Add); return;
    case NodeKind::Sub: binary(Op::Sub); return;
    case NodeKind::Mul: binary(Op::Mul); return;
    case NodeKind::Div: binary(Op::Div); return;
    case NodeKind::Less: binary(Op::Less); p.hasTruthOps = true; return;
    case NodeKind::LessEq: binary(Op::LessEq); p.hasTruthOps = true; return;
    case NodeKind::Greater: binary(Op::Greater); p.hasTruthOps = true; return;
    case NodeKind::GreaterEq: binary(Op::GreaterEq); p.hasTruthOps = true; return;
    case NodeKind::Equal: binary(Op::Equal); p.hasTruthOps = true; return;
    case NodeKind::NotEqual: binary(Op::NotEqual); p.hasTruthOps = true; return;
    case NodeKind::And: binary(Op::And); p.hasTruthOps = true; return;
    case NodeKind::Or: binary(Op::Or); p.hasTruthOps = true; return;
    default: return;
    }
}
}

Program Program::compile(const Node &root)
{
    Program p;
    emit(root, p);
    // worst-case stack depth = number of pushes never exceeds code size
    p.maxStack = static_cast<int>(p.code.size()) + 1;
    return p;
}

double Program::evalPoint(double x, double y, std::vector<double> &st) const
{
    st.clear();
    for (const auto &in : code)
    {
        switch (in.op)
        {
        case Op::PushConst: st.push_back(in.imm); break;
        case Op::PushVar: st.push_back(in.slot == 0 ? x : y); break;
        case Op::Neg: st.back() = -st.back(); break;
        case Op::Abs: st.back() = std::abs(st.back()); break;
        case Op::Sin: st.back() = std::sin(st.back()); break;
        case Op::Cos: st.back() = std::cos(st.back()); break;
        case Op::Tan: st.back() = std::tan(st.back()); break;
        case Op::Asin: st.back() = std::asin(st.back()); break;
        case Op::Acos: st.back() = std::acos(st.back()); break;
        case Op::Atan: st.back() = std::atan(st.back()); break;
        case Op::Log: st.back() = std::log(st.back()); break;
        case Op::Exp: st.back() = std::exp(st.back()); break;
        case Op::Sqrt: st.back() = std::sqrt(st.back()); break;
        case Op::Floor: st.back() = std::floor(st.back()); break;
        case Op::Ceil: st.back() = std::ceil(st.back()); break;
        case Op::Sign:
            st.back() = st.back() > 0.0 ? 1.0 : st.back() < 0.0 ? -1.0 : st.back();
            break;
        case Op::IPow: st.back() = std::pow(st.back(), in.imm); break;
        default:
        {
            const double b = st.back();
            st.pop_back();
            double &a = st.back();
            switch (in.op)
            {
            case Op::Min: a = std::min(a, b); break;
            case Op::Max: a = std::max(a, b); break;
            case Op::Add: a = a + b; break;
            case Op::Sub: a = a - b; break;
            case Op::Mul: a = a * b; break;
            case Op::Div: a = a / b; break;
            case Op::Pow: a = std::pow(a, b); break;
            case Op::Less: a = (a < b) ? 1.0 : 0.0; break;
            case Op::LessEq: a = (a <= b) ? 1.0 : 0.0; break;
            case Op::Greater: a = (a > b) ? 1.0 : 0.0; break;
            case Op::GreaterEq: a = (a >= b) ? 1.0 : 0.0; break;
            case Op::Equal: a = (a == b) ? 1.0 : 0.0; break;
            case Op::NotEqual: a = (a != b) ? 1.0 : 0.0; break;
            case Op::And: a = (a != 0.0 && b != 0.0) ? 1.0 : 0.0; break;
            case Op::Or: a = (a != 0.0 || b != 0.0) ? 1.0 : 0.0; break;
            default: break;
            }
            break;
        }
        }
    }
    return st.empty() ? 0.0 : st.back();
}

void Program::evalPointBatch(const double *xs, const double *ys, double *out, int n,
                             std::vector<double> &slab) const
{
    // Op-outer interpretation: each instruction sweeps a whole chunk of points,
    // so the dispatch cost is paid once per op (not once per sample) and the
    // arithmetic loops auto-vectorize. Chunks keep the working set in L1/L2.
    constexpr int kChunk = 256;
    static_assert(kChunk % kLanes == 0);
    const int rows = std::max(1, maxStack);
    if (slab.size() < static_cast<size_t>(rows) * kChunk)
        slab.resize(static_cast<size_t>(rows) * kChunk);
    // Per-slot "is a splat of this constant" flags enable the const-base pow
    // rewrite (c^b -> exp2(b*log2 c)) -- the path `2^x` takes (log2(2)==1).
    double cval[64];
    bool cflag[64];

    for (int base = 0; base < n; base += kChunk)
    {
        const int w = std::min(kChunk, n - base);
        const int pw = ((w + kLanes - 1) / kLanes) * kLanes; // padded width
        auto row = [&](int k) { return slab.data() + static_cast<size_t>(k) * kChunk; };
        int sp = 0;

        for (const auto &in : code)
        {
            switch (in.op)
            {
            case Op::PushConst:
            {
                double *r = row(sp);
                for (int i = 0; i < pw; ++i) r[i] = in.imm;
                if (sp < 64)
                {
                    cval[sp] = in.imm;
                    cflag[sp] = true;
                }
                ++sp;
                break;
            }
            case Op::PushVar:
            {
                double *r = row(sp);
                const double *src = in.slot == 0 ? xs + base : ys + base;
                for (int i = 0; i < w; ++i) r[i] = src[i];
                for (int i = w; i < pw; ++i) r[i] = src[w - 1]; // pad: replicate
                if (sp < 64) cflag[sp] = false;
                ++sp;
                break;
            }
            case Op::Neg:
            {
                double *r = row(sp - 1);
                for (int i = 0; i < pw; ++i) r[i] = -r[i];
                if (sp - 1 < 64) cflag[sp - 1] = false;
                break;
            }
            case Op::Abs:
            {
                double *r = row(sp - 1);
                for (int i = 0; i < pw; ++i) r[i] = std::abs(r[i]);
                if (sp - 1 < 64) cflag[sp - 1] = false;
                break;
            }
            case Op::Sin:
            case Op::Cos:
            case Op::Tan:
            {
                double *r = row(sp - 1);
                for (int i = 0; i < pw; i += kLanes)
                {
                    vd a, s, c;
                    for (int j = 0; j < kLanes; ++j) a.v[j] = r[i + j];
                    vsincos(a, s, c);
                    if (in.op == Op::Sin)
                        for (int j = 0; j < kLanes; ++j) r[i + j] = s.v[j];
                    else if (in.op == Op::Cos)
                        for (int j = 0; j < kLanes; ++j) r[i + j] = c.v[j];
                    else
                        for (int j = 0; j < kLanes; ++j) r[i + j] = s.v[j] / c.v[j];
                }
                if (sp - 1 < 64) cflag[sp - 1] = false;
                break;
            }
            case Op::Exp:
            {
                double *r = row(sp - 1);
                for (int i = 0; i < pw; i += kLanes)
                {
                    vd a;
                    for (int j = 0; j < kLanes; ++j) a.v[j] = r[i + j];
                    const vd e = vexp(a);
                    for (int j = 0; j < kLanes; ++j) r[i + j] = e.v[j];
                }
                if (sp - 1 < 64) cflag[sp - 1] = false;
                break;
            }
            case Op::Asin:
            {
                double *r = row(sp - 1);
                for (int i = 0; i < pw; ++i) r[i] = std::asin(r[i]);
                if (sp - 1 < 64) cflag[sp - 1] = false;
                break;
            }
            case Op::Acos:
            {
                double *r = row(sp - 1);
                for (int i = 0; i < pw; ++i) r[i] = std::acos(r[i]);
                if (sp - 1 < 64) cflag[sp - 1] = false;
                break;
            }
            case Op::Atan:
            {
                double *r = row(sp - 1);
                for (int i = 0; i < pw; ++i) r[i] = std::atan(r[i]);
                if (sp - 1 < 64) cflag[sp - 1] = false;
                break;
            }
            case Op::Log:
            {
                double *r = row(sp - 1);
                for (int i = 0; i < pw; ++i) r[i] = std::log(r[i]);
                if (sp - 1 < 64) cflag[sp - 1] = false;
                break;
            }
            case Op::Sqrt:
            {
                double *r = row(sp - 1);
                for (int i = 0; i < pw; ++i) r[i] = std::sqrt(r[i]);
                if (sp - 1 < 64) cflag[sp - 1] = false;
                break;
            }
            case Op::Floor:
            {
                double *r = row(sp - 1);
                for (int i = 0; i < pw; ++i) r[i] = std::floor(r[i]);
                if (sp - 1 < 64) cflag[sp - 1] = false;
                break;
            }
            case Op::Ceil:
            {
                double *r = row(sp - 1);
                for (int i = 0; i < pw; ++i) r[i] = std::ceil(r[i]);
                if (sp - 1 < 64) cflag[sp - 1] = false;
                break;
            }
            case Op::Sign:
            {
                double *r = row(sp - 1);
                for (int i = 0; i < pw; ++i)
                    r[i] = r[i] > 0.0 ? 1.0 : r[i] < 0.0 ? -1.0 : r[i];
                if (sp - 1 < 64) cflag[sp - 1] = false;
                break;
            }
            case Op::IPow:
            {
                double *r = row(sp - 1);
                const long long e = static_cast<long long>(in.imm);
                if (e == 2)
                    for (int i = 0; i < pw; ++i) r[i] = r[i] * r[i];
                else if (e == 3)
                    for (int i = 0; i < pw; ++i) r[i] = r[i] * r[i] * r[i];
                else if (e == 4)
                    for (int i = 0; i < pw; ++i)
                    {
                        const double s = r[i] * r[i];
                        r[i] = s * s;
                    }
                else
                    for (int i = 0; i < pw; ++i) r[i] = std::pow(r[i], in.imm);
                if (sp - 1 < 64) cflag[sp - 1] = false;
                break;
            }
            default: // binary ops
            {
                double *a = row(sp - 2);
                const double *b = row(sp - 1);
                switch (in.op)
                {
                case Op::Add:
                    for (int i = 0; i < pw; ++i) a[i] = a[i] + b[i];
                    break;
                case Op::Sub:
                    for (int i = 0; i < pw; ++i) a[i] = a[i] - b[i];
                    break;
                case Op::Mul:
                    for (int i = 0; i < pw; ++i) a[i] = a[i] * b[i];
                    break;
                case Op::Min:
                    for (int i = 0; i < pw; ++i) a[i] = std::min(a[i], b[i]);
                    break;
                case Op::Max:
                    for (int i = 0; i < pw; ++i) a[i] = std::max(a[i], b[i]);
                    break;
                case Op::Div:
                    for (int i = 0; i < pw; ++i) a[i] = a[i] / b[i];
                    break;
                case Op::Pow:
                {
                    const bool constBase = sp - 2 < 64 && cflag[sp - 2];
                    const double c = constBase ? cval[sp - 2] : 0.0;
                    if (constBase && c == 1.0)
                    {
                        // IEEE: 1^anything (even NaN) is 1
                        for (int i = 0; i < pw; ++i) a[i] = 1.0;
                    }
                    else if (constBase && c > 0.0 && std::isfinite(c))
                    {
                        // c^b = 2^(b*log2 c); for the wall formula 2^x this is
                        // EXACT in the exponent (log2(2)==1) and pure vexp2.
                        const double l2c = std::log2(c);
                        for (int i = 0; i < pw; i += kLanes)
                        {
                            vd t;
                            for (int j = 0; j < kLanes; ++j) t.v[j] = b[i + j] * l2c;
                            const vd e = vexp2(t);
                            for (int j = 0; j < kLanes; ++j) a[i + j] = e.v[j];
                        }
                    }
                    else
                    {
                        for (int i = 0; i < pw; ++i) a[i] = std::pow(a[i], b[i]);
                    }
                    break;
                }
                case Op::Less:
                    for (int i = 0; i < pw; ++i) a[i] = (a[i] < b[i]) ? 1.0 : 0.0;
                    break;
                case Op::LessEq:
                    for (int i = 0; i < pw; ++i) a[i] = (a[i] <= b[i]) ? 1.0 : 0.0;
                    break;
                case Op::Greater:
                    for (int i = 0; i < pw; ++i) a[i] = (a[i] > b[i]) ? 1.0 : 0.0;
                    break;
                case Op::GreaterEq:
                    for (int i = 0; i < pw; ++i) a[i] = (a[i] >= b[i]) ? 1.0 : 0.0;
                    break;
                case Op::Equal:
                    for (int i = 0; i < pw; ++i) a[i] = (a[i] == b[i]) ? 1.0 : 0.0;
                    break;
                case Op::NotEqual:
                    for (int i = 0; i < pw; ++i) a[i] = (a[i] != b[i]) ? 1.0 : 0.0;
                    break;
                case Op::And:
                    for (int i = 0; i < pw; ++i)
                        a[i] = (a[i] != 0.0 && b[i] != 0.0) ? 1.0 : 0.0;
                    break;
                case Op::Or:
                    for (int i = 0; i < pw; ++i)
                        a[i] = (a[i] != 0.0 || b[i] != 0.0) ? 1.0 : 0.0;
                    break;
                default: break;
                }
                if (sp - 2 < 64) cflag[sp - 2] = false;
                --sp;
                break;
            }
            }
        }

        const double *res = sp > 0 ? row(sp - 1) : nullptr;
        for (int i = 0; i < w; ++i) out[base + i] = res ? res[i] : 0.0;
    }
}

Interval Program::evalInterval(const Interval &x, const Interval &y, std::vector<Interval> &st) const
{
    st.clear();
    for (const auto &in : code)
    {
        switch (in.op)
        {
        case Op::PushConst: st.push_back(Interval{in.imm}); break;
        case Op::PushVar: st.push_back(in.slot == 0 ? x : y); break;
        case Op::Neg: st.back() = -st.back(); break;
        case Op::Abs: st.back() = iabs(st.back()); break;
        case Op::Sin: st.back() = sin(st.back()); break;
        case Op::Cos: st.back() = cos(st.back()); break;
        case Op::Tan: st.back() = tan(st.back()); break;
        case Op::Asin: st.back() = asin(st.back()); break;
        case Op::Acos: st.back() = acos(st.back()); break;
        case Op::Atan: st.back() = atan(st.back()); break;
        case Op::Log: st.back() = log(st.back()); break;
        case Op::Exp: st.back() = exp(st.back()); break;
        case Op::Sqrt: st.back() = sqrt(st.back()); break;
        case Op::Floor: st.back() = ifloor(st.back()); break;
        case Op::Ceil: st.back() = iceil(st.back()); break;
        case Op::Sign: st.back() = isign(st.back()); break;
        case Op::IPow: st.back() = ipow(st.back(), static_cast<long long>(in.imm)); break;
        default:
        {
            const Interval b = st.back();
            st.pop_back();
            Interval &a = st.back();
            switch (in.op)
            {
            case Op::Add: a = a + b; break;
            case Op::Sub: a = a - b; break;
            case Op::Mul: a = a * b; break;
            case Op::Div: a = a / b; break;
            case Op::Pow: a = pow(a, b); break;
            case Op::Min: a = imin(a, b); break;
            case Op::Max: a = imax(a, b); break;
            case Op::Less: a = cmpLess(a, b); break;
            case Op::LessEq: a = cmpLessEq(a, b); break;
            case Op::Greater: a = cmpGreater(a, b); break;
            case Op::GreaterEq: a = cmpGreaterEq(a, b); break;
            case Op::Equal: a = cmpEqual(a, b); break;
            case Op::NotEqual: a = cmpNotEqual(a, b); break;
            case Op::And: a = logicAnd(a, b); break;
            case Op::Or: a = logicOr(a, b); break;
            default: break;
            }
            break;
        }
        }
    }
    return st.empty() ? Interval{0.0} : st.back();
}

Jet Program::evalJet(const Interval &x, const Interval &y, std::vector<Jet> &st) const
{
    const Interval zero{0.0};
    const Interval one{1.0};
    st.clear();
    for (const auto &in : code)
    {
        switch (in.op)
        {
        case Op::PushConst: st.push_back(Jet{Interval{in.imm}, zero, zero}); break;
        case Op::PushVar:
            st.push_back(in.slot == 0 ? Jet{x, one, zero} : Jet{y, zero, one});
            break;
        case Op::Neg:
        {
            Jet &a = st.back();
            a.v = -a.v;
            a.dx = -a.dx;
            a.dy = -a.dy;
            break;
        }
        case Op::Abs:
        {
            Jet &a = st.back();
            Interval f = a.v.allPositive() ? one : a.v.allNegative() ? Interval{-1.0}
                                                                     : Interval{-1.0, 1.0};
            a.v = iabs(a.v);
            a.dx = f * a.dx;
            a.dy = f * a.dy;
            break;
        }
        case Op::Sin:
        {
            Jet &a = st.back();
            Interval c = cos(a.v);
            a.dx = c * a.dx;
            a.dy = c * a.dy;
            a.v = sin(a.v);
            break;
        }
        case Op::Cos:
        {
            Jet &a = st.back();
            Interval c = -sin(a.v);
            a.dx = c * a.dx;
            a.dy = c * a.dy;
            a.v = cos(a.v);
            break;
        }
        case Op::Tan:
        {
            Jet &a = st.back();
            Interval t = tan(a.v);
            Interval c = one + t * t; // sec^2
            a.dx = c * a.dx;
            a.dy = c * a.dy;
            a.v = t;
            break;
        }
        case Op::Asin:
        {
            Jet &a = st.back();
            Interval c = one / sqrt(one - a.v * a.v); // straddles +-1 -> whole
            a.dx = c * a.dx;
            a.dy = c * a.dy;
            a.v = asin(a.v);
            break;
        }
        case Op::Acos:
        {
            Jet &a = st.back();
            Interval c = -(one / sqrt(one - a.v * a.v));
            a.dx = c * a.dx;
            a.dy = c * a.dy;
            a.v = acos(a.v);
            break;
        }
        case Op::Atan:
        {
            Jet &a = st.back();
            Interval c = one / (one + a.v * a.v); // always finite
            a.dx = c * a.dx;
            a.dy = c * a.dy;
            a.v = atan(a.v);
            break;
        }
        case Op::Exp:
        {
            Jet &a = st.back();
            Interval e = exp(a.v);
            a.dx = e * a.dx;
            a.dy = e * a.dy;
            a.v = e;
            break;
        }
        case Op::Log:
        {
            Jet &a = st.back();
            Interval c = one / a.v;
            a.dx = c * a.dx;
            a.dy = c * a.dy;
            a.v = log(a.v);
            break;
        }
        case Op::Sqrt:
        {
            Jet &a = st.back();
            Interval s = sqrt(a.v);
            Interval c = one / (Interval{2.0} * s); // s contains 0 -> whole
            a.dx = c * a.dx;
            a.dy = c * a.dy;
            a.v = s;
            break;
        }
        case Op::IPow:
        {
            Jet &a = st.back();
            const long long n = static_cast<long long>(in.imm);
            Interval c = Interval{static_cast<double>(n)} * ipow(a.v, n - 1);
            a.dx = c * a.dx;
            a.dy = c * a.dy;
            a.v = ipow(a.v, n);
            break;
        }
        case Op::Floor:
        case Op::Ceil:
        case Op::Sign:
        {
            // Piecewise-constant: derivative 0 almost everywhere; the jumps
            // are reported by the value interval's disc flag, which soundly
            // blocks any uniform proof across them -- so a zero gradient is a
            // sound mean-value enclosure on every jump-free box.
            Jet &a = st.back();
            a.v = in.op == Op::Floor ? ifloor(a.v)
                  : in.op == Op::Ceil ? iceil(a.v)
                                      : isign(a.v);
            a.dx = Interval{0.0, 0.0};
            a.dy = Interval{0.0, 0.0};
            break;
        }
        default:
        {
            Jet b = st.back();
            st.pop_back();
            Jet &a = st.back();
            switch (in.op)
            {
            case Op::Add:
                a.v = a.v + b.v;
                a.dx = a.dx + b.dx;
                a.dy = a.dy + b.dy;
                break;
            case Op::Min:
            case Op::Max:
            {
                const bool useMin = in.op == Op::Min;
                const bool aWins = useMin ? a.v.hi < b.v.lo : a.v.lo > b.v.hi;
                const bool bWins = useMin ? b.v.hi < a.v.lo : b.v.lo > a.v.hi;
                a.v = useMin ? imin(a.v, b.v) : imax(a.v, b.v);
                if (bWins)
                {
                    a.dx = b.dx;
                    a.dy = b.dy;
                }
                else if (!aWins)
                {
                    // kink region: the subgradient hull encloses both branches
                    a.dx = ihull(a.dx, b.dx);
                    a.dy = ihull(a.dy, b.dy);
                }
                break;
            }
            case Op::Sub:
                a.v = a.v - b.v;
                a.dx = a.dx - b.dx;
                a.dy = a.dy - b.dy;
                break;
            case Op::Mul:
            {
                Interval av = a.v, bv = b.v;
                a.dx = av * b.dx + bv * a.dx;
                a.dy = av * b.dy + bv * a.dy;
                a.v = av * bv;
                break;
            }
            case Op::Div:
            {
                Interval av = a.v, bv = b.v;
                Interval bb = bv * bv;
                a.dx = (a.dx * bv - av * b.dx) / bb;
                a.dy = (a.dy * bv - av * b.dy) / bb;
                a.v = av / bv;
                break;
            }
            case Op::Pow:
                // general exponent: conservative gradient
                a.v = pow(a.v, b.v);
                a.dx = Interval::whole();
                a.dy = Interval::whole();
                break;
            default:
                // truth ops should not appear in a jet-evaluated program
                a.v = Interval{0.0};
                a.dx = zero;
                a.dy = zero;
                break;
            }
            break;
        }
        }
    }
    return st.empty() ? Jet{Interval{0.0}, zero, zero} : st.back();
}
}
