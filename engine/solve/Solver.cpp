#include "Solver.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gxr
{
namespace
{
struct Box
{
    int x0, y0, x1, y1; // sub-cell coordinates; always square (x1-x0 == y1-y0)
};

// half line-width of the rendered equality curve, in pixels
constexpr double kCurveHalfWidthPx = 0.6;

class TileSolver
{
public:
    TileSolver(const Relation &rel, const WorldRect &rect, const SolveParams &p, EvalScratch &s,
               const CancelToken &cancel)
        : rel_(rel), rect_(rect), s_(s), cancel_(cancel), T_(p.tilePx), K_(p.subBits),
          budget_(p.boxBudget)
    {
        pixelSub_ = 1 << K_;
        N_ = T_ * pixelSub_;
        stepX_ = rect_.width() / N_;
        stepY_ = rect_.height() / N_;
        wpp_ = rect_.width() / T_;
        equalityCurve_ = rel_.isEquality();
        notEqual_ = rel_.isNotEqual();
        acc_.assign(static_cast<size_t>(T_) * T_, 0.0);
        unc_.assign(static_cast<size_t>(T_) * T_, 0.0);
    }

    CoverageTile run()
    {
        // Depth-first traversal: the work stack stays O(tree depth) instead of
        // the O(frontier) of a breadth-first sweep, so even a fully-uncertain
        // tile keeps a tiny, cache-resident working set (orders of magnitude
        // faster per box than a multi-hundred-MB BFS frontier).
        std::vector<Box> stack;
        stack.reserve(256);
        stack.push_back(Box{0, 0, N_, N_});
        bool bailed = false;

        while (!stack.empty())
        {
            if (++processed_ > budget_)
            {
                bailed = true;
                break;
            }
            if ((processed_ & 2047) == 0 && cancel_.cancelled())
            {
                bailed = true;
                break;
            }
            const Box b = stack.back();
            stack.pop_back();
            processBox(b, stack);
        }

        if (bailed)
        {
            for (const Box &b : stack) addHalf(b); // estimate the unprocessed frontier
        }
        return finalize(!bailed);
    }

private:
    const Relation &rel_;
    WorldRect rect_;
    EvalScratch &s_;
    CancelToken cancel_;
    int T_, K_, pixelSub_, N_;
    long long budget_;
    long long processed_{0};
    double stepX_, stepY_, wpp_;
    bool equalityCurve_, notEqual_;
    bool useCentered_{true};
    long long centeredAttempts_{0};
    long long centeredWins_{0};
    std::vector<double> acc_; // covered sub-cell area per pixel
    std::vector<double> unc_; // boundary/estimated sub-cell count per pixel

    // Naive interval first; escalate to the centered (mean-value) form only while
    // it is still earning its cost. On formulas where it never certifies a box
    // (e.g. genuine 2-D oscillation), it is disabled after a sampling window so
    // the tile stops paying ~3x per box for nothing.
    Sign classifyAdaptive(const Interval &ix, const Interval &iy)
    {
        const Sign sn = rel_.classifyNaive(ix, iy, s_);
        if (sn != Sign::Uncertain || !useCentered_) return sn;
        const Sign sc = rel_.classifyRefined(ix, iy, s_);
        ++centeredAttempts_;
        if (sc != Sign::Uncertain) ++centeredWins_;
        if (centeredAttempts_ == 1024 && centeredWins_ * 50 < centeredAttempts_)
        {
            useCentered_ = false; // <2% certify rate -> not worth it for this tile
        }
        return sc;
    }

    [[nodiscard]] Interval xInterval(int a, int b) const
    {
        return Interval{rect_.x0 + a * stepX_, rect_.x0 + b * stepX_};
    }
    [[nodiscard]] Interval yInterval(int a, int b) const
    {
        return Interval{rect_.y0 + a * stepY_, rect_.y0 + b * stepY_};
    }

    void processBox(const Box &b, std::vector<Box> &next)
    {
        const Interval ix = xInterval(b.x0, b.x1);
        const Interval iy = yInterval(b.y0, b.y1);
        const Sign sign = classifyAdaptive(ix, iy);

        if (sign == Sign::AllFalse || sign == Sign::Undefined) return;
        if (sign == Sign::AllTrue)
        {
            addUniform(b, 1.0, 0.0);
            return;
        }
        // Uncertain (boundary, discontinuity, or equality curve).
        // Inequalities descend to a sub-pixel cell (unbiased sampling).
        // Equalities stop at pixel level, where the gradient band IS the AA line
        // (descending further would only thin the measure-zero curve to nothing).
        const int sz = b.x1 - b.x0;
        const int floorSize = equalityCurve_ ? pixelSub_ : 1;
        if (sz <= floorSize)
        {
            addFloor(b, ix, iy);
            return;
        }
        const int mx = (b.x0 + b.x1) / 2;
        const int my = (b.y0 + b.y1) / 2;
        next.push_back(Box{b.x0, b.y0, mx, my});
        next.push_back(Box{mx, b.y0, b.x1, my});
        next.push_back(Box{b.x0, my, mx, b.y1});
        next.push_back(Box{mx, my, b.x1, b.y1});
    }

    // Distribute a uniform coverage fraction over the pixels a box overlaps.
    void addUniform(const Box &b, double covFrac, double uncFrac)
    {
        const int sz = b.x1 - b.x0;
        if (sz >= pixelSub_)
        {
            const int px0 = b.x0 >> K_, px1 = b.x1 >> K_;
            const int py0 = b.y0 >> K_, py1 = b.y1 >> K_;
            const double full = static_cast<double>(pixelSub_) * pixelSub_;
            for (int py = py0; py < py1; ++py)
                for (int px = px0; px < px1; ++px)
                {
                    const size_t idx = static_cast<size_t>(py) * T_ + px;
                    acc_[idx] += covFrac * full;
                    unc_[idx] += uncFrac * full;
                }
        }
        else
        {
            const int px = b.x0 >> K_, py = b.y0 >> K_;
            const size_t idx = static_cast<size_t>(py) * T_ + px;
            const double area = static_cast<double>(sz) * sz;
            acc_[idx] += covFrac * area;
            unc_[idx] += uncFrac * area;
        }
    }

    void addHalf(const Box &b) { addUniform(b, 0.5, 1.0); }

    // Floor leaf. For inequalities this is a single sub-pixel cell, resolved by
    // an unbiased center sample. For equalities it is a whole pixel, where the
    // gradient band gives a resolution-independent ~1px anti-aliased line.
    void addFloor(const Box &b, const Interval &ix, const Interval &iy)
    {
        const int px = b.x0 >> K_, py = b.y0 >> K_;
        const size_t idx = static_cast<size_t>(py) * T_ + px;

        if (!equalityCurve_)
        {
            const double cx = rect_.x0 + (b.x0 + 0.5) * stepX_;
            const double cy = rect_.y0 + (b.y0 + 0.5) * stepY_;
            acc_[idx] += rel_.pointInside(cx, cy, s_) ? 1.0 : 0.0;
            unc_[idx] += 1.0;
            return;
        }

        // equality: one pixel-sized box
        const double cx = rect_.x0 + (b.x0 + b.x1) * 0.5 * stepX_;
        const double cy = rect_.y0 + (b.y0 + b.y1) * 0.5 * stepY_;
        double cov = bandCoverage(ix, iy, cx, cy);
        if (notEqual_) cov = 1.0 - cov;
        const double area = static_cast<double>(b.x1 - b.x0) * (b.y1 - b.y0);
        acc_[idx] += cov * area;
        unc_[idx] += area;
    }

    // Coverage of a sub-cell by the ~1px-wide curve band of f=0.
    double bandCoverage(const Interval &ix, const Interval &iy, double cx, double cy)
    {
        Interval val, gx, gy;
        rel_.valueAndGrad(ix, iy, s_, val, gx, gy);
        const double fmid = rel_.fValue(cx, cy, s_);
        const double gmag = std::hypot(gx.mid(), gy.mid());
        if (!std::isfinite(fmid) || !std::isfinite(gmag) || gmag <= 0.0)
        {
            return val.straddlesZero() ? 1.0 : 0.0;
        }
        const double distPx = std::abs(fmid) / (gmag * wpp_);
        return std::clamp(kCurveHalfWidthPx + 0.5 - distPx, 0.0, 1.0);
    }

    CoverageTile finalize(bool converged)
    {
        CoverageTile tile;
        tile.width = T_;
        tile.height = T_;
        tile.subBits = K_;
        tile.converged = converged;
        tile.alpha.resize(static_cast<size_t>(T_) * T_);
        const double perPixel = static_cast<double>(pixelSub_) * pixelSub_;
        float worst = 0.0f;
        for (size_t i = 0; i < tile.alpha.size(); ++i)
        {
            tile.alpha[i] = static_cast<float>(std::clamp(acc_[i] / perPixel, 0.0, 1.0));
            worst = std::max(worst, static_cast<float>(unc_[i] / perPixel));
        }
        tile.worstUncertainty = worst;
        return tile;
    }
};
}

namespace
{
// Coverage of one pixel (y in [ya,yb]) for an explicit-y inequality, given the
// sorted finite samples of g(x) across the pixel column and their prefix sums.
// `greater` = true region is y > g.  Samples that were undefined (NaN) are
// excluded from `g` but still counted in `S` (they contribute zero coverage).
double columnPixelCoverage(const std::vector<double> &g, const std::vector<double> &prefix,
                           double ya, double yb, double dy, int S, bool greater)
{
    const auto loAt = [&](double v) {
        return static_cast<size_t>(std::lower_bound(g.begin(), g.end(), v) - g.begin());
    };
    const auto upAt = [&](double v) {
        return static_cast<size_t>(std::upper_bound(g.begin(), g.end(), v) - g.begin());
    };
    if (greater)
    {
        // fraction of [ya,yb] above g_k, averaged: g<=ya -> 1, g in (ya,yb) -> (yb-g)/dy
        const size_t aIdx = upAt(ya);
        const size_t bIdx = loAt(yb);
        const double sumMid = prefix[bIdx] - prefix[aIdx];
        const double midCount = static_cast<double>(bIdx - aIdx);
        const double partial = (midCount * yb - sumMid) / dy;
        return std::clamp((static_cast<double>(aIdx) + partial) / S, 0.0, 1.0);
    }
    // less: g>=yb -> 1, g in (ya,yb) -> (g-ya)/dy
    const size_t aIdx = upAt(ya);
    const size_t bIdx = loAt(yb);
    const size_t highCount = g.size() - bIdx;
    const double sumMid = prefix[bIdx] - prefix[aIdx];
    const double midCount = static_cast<double>(bIdx - aIdx);
    const double partial = (sumMid - midCount * ya) / dy;
    return std::clamp((static_cast<double>(highCount) + partial) / S, 0.0, 1.0);
}

// Direct 1-D solver for `v <op> g(w)` (v,w are x/y in either order): reduces the
// 2-D area to a per-line quadrature of g. Exact for smooth g; for sub-pixel-
// oscillating g it converges to the true Lebesgue measure (unbiased) -> stable
// smooth gray. Far cheaper than 2-D subdivision and free of the half-floor bias.
CoverageTile solveExplicit1D(const Relation &rel, const WorldRect &rect, const SolveParams &params,
                             EvalScratch &scratch, const CancelToken &cancel)
{
    const int T = params.tilePx;
    const Program &G = *rel.explicitG();
    const CmpOp op = rel.explicitOp();
    const bool greater = (op == CmpOp::Greater || op == CmpOp::GreaterEq);
    const bool yExplicit = rel.explicitIsY(); // y<op>g(x): sample over x. else x<op>g(y).
    // Samples per line scale with the refinement pass; more samples -> lower
    // per-line measure variance -> smoother gray in the sub-pixel regime.
    const int S = std::clamp(128 << params.subBits, 128, 8192);
    const double wppX = rect.width() / T;
    const double wppY = rect.height() / T;

    // outer axis = w (the function input), inner axis = v (the isolated variable)
    const double wOrigin = yExplicit ? rect.x0 : rect.y0;
    const double wStepPix = yExplicit ? wppX : wppY;
    const double vOrigin = yExplicit ? rect.y0 : rect.x0;
    const double vStepPix = yExplicit ? wppY : wppX;

    CoverageTile tile;
    tile.width = tile.height = T;
    tile.subBits = params.subBits;
    tile.converged = true;
    tile.alpha.assign(static_cast<size_t>(T) * T, 0.0f);

    std::vector<double> g;
    std::vector<double> prefix;
    g.reserve(S);
    prefix.reserve(S + 1);

    for (int o = 0; o < T; ++o)
    {
        if ((o & 31) == 0 && cancel.cancelled())
        {
            tile.converged = false;
            break;
        }
        const double wa = wOrigin + o * wStepPix;
        g.clear();
        for (int k = 0; k < S; ++k)
        {
            const double w = wa + (k + 0.5) * wStepPix / S;
            const double val = yExplicit ? G.evalPoint(w, 0.0, scratch.sd)
                                         : G.evalPoint(0.0, w, scratch.sd);
            if (std::isfinite(val)) g.push_back(val); // undefined -> 0, still in S
        }
        std::sort(g.begin(), g.end());
        prefix.assign(g.size() + 1, 0.0);
        for (size_t k = 0; k < g.size(); ++k) prefix[k + 1] = prefix[k] + g[k];

        for (int i = 0; i < T; ++i)
        {
            const double va = vOrigin + i * vStepPix;
            const double cov = columnPixelCoverage(g, prefix, va, va + vStepPix, vStepPix, S, greater);
            const size_t idx = yExplicit ? static_cast<size_t>(i) * T + o
                                         : static_cast<size_t>(o) * T + i;
            tile.alpha[idx] = static_cast<float>(cov);
        }
    }
    return tile;
}
}

CoverageTile solveTile(const Relation &rel, const WorldRect &rect, const SolveParams &params,
                       EvalScratch &scratch, const CancelToken &cancel)
{
    // Fast, exact-measure path for explicit-1D inequalities (v <op> g(w)).
    if (params.analytic && rel.explicit1D() && !rel.isEquality())
    {
        return solveExplicit1D(rel, rect, params, scratch, cancel);
    }
    TileSolver solver(rel, rect, params, scratch, cancel);
    return solver.run();
}
}
