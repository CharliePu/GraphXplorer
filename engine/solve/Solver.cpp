#include "Solver.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
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

// Deterministic, world-aligned sample jitter. Stratified samples at fixed
// in-cell offsets phase-lock against a regular oscillation and render as
// streak artifacts; hashing the stratum's WORLD origin into the offset turns
// that aliasing into unbiased MC noise while keeping the tile a pure function
// of world position (same region -> identical tile -> no temporal flicker).
inline uint64_t mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

// two offsets in [0,1) for the stratum whose world origin is (wx, wy)
inline void jitter2(double wx, double wy, double &jx, double &jy)
{
    uint64_t h = mix64(std::bit_cast<uint64_t>(wx) ^ 0x9e3779b97f4a7c15ULL);
    h = mix64(h ^ std::bit_cast<uint64_t>(wy));
    jx = static_cast<double>(h >> 11) * 0x1.0p-53;
    jy = static_cast<double>(mix64(h) >> 11) * 0x1.0p-53;
}

class TileSolver
{
public:
    TileSolver(const Relation &rel, const WorldRect &rect, const SolveParams &p, EvalScratch &s,
               const CancelToken &cancel)
        : rel_(rel), rect_(rect), s_(s), cancel_(cancel), T_(p.tilePx), K_(p.subBits),
          budget_(p.boxBudget)
    {
        floorN_ = std::clamp(p.floorSamples, 1, 16);
        bailM_ = std::clamp(p.bailSamples, 1, 16); // 16x16 = the 256-sample batch ceiling
        pixelSub_ = 1 << K_;
        N_ = T_ * pixelSub_;
        stepX_ = rect_.width() / N_;
        stepY_ = rect_.height() / N_;
        wpp_ = rect_.width() / T_;
        equalityCurve_ = rel_.isEquality();
        notEqual_ = rel_.isNotEqual();
        closedBand_ = rel_.isClosedInequality();
        bandFloor_ = std::max(1, pixelSub_ / 2);
        acc_.assign(static_cast<size_t>(T_) * T_, 0.0);
        unc_.assign(static_cast<size_t>(T_) * T_, 0.0);
        if (closedBand_) bandAcc_.assign(static_cast<size_t>(T_) * T_, 0.0);
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
            if ((processed_ & 511) == 0 && cancel_.cancelled())
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
            for (const Box &b : stack)
            {
                if (cancel_.cancelled()) break; // abandoned: result is never published
                addHalf(b);                     // estimate the unprocessed frontier
            }
        }
        return finalize(!bailed);
    }

private:
    const Relation &rel_;
    WorldRect rect_;
    EvalScratch &s_;
    CancelToken cancel_;
    int T_, K_, pixelSub_, N_;
    int floorN_{1}; // per-axis sub-samples for an uncertain floor/bailout cell
    int bailM_{16}; // per-axis samples for a budget-bailed pixel

    // Deferred floor-cell sampling: a single uncertain floor cell only needs
    // floorN^2 (typically 4) samples -- far too few to amortize a batch call.
    // Cells queue their samples here and flush 256 at a time, so the SIMD
    // kernels always run on full batches. Flush order is deterministic.
    static constexpr int kPendCap = 256;
    double pendX_[kPendCap], pendY_[kPendCap];
    size_t pendPix_[kPendCap]; // target pixel per CELL (samples are cell-contiguous)
    int pendSamples_{0};

    void flushFloorPending()
    {
        if (pendSamples_ == 0) return;
        unsigned char in[kPendCap];
        rel_.pointInsideMask(pendX_, pendY_, pendSamples_, in, s_);
        const int spc = floorN_ * floorN_;
        const double invTotal = 1.0 / spc;
        int cell = 0;
        for (int s = 0; s < pendSamples_; s += spc, ++cell)
        {
            int hits = 0;
            for (int j = 0; j < spc; ++j) hits += in[s + j];
            const double frac = hits * invTotal;
            acc_[pendPix_[cell]] += frac;
            unc_[pendPix_[cell]] += frac * (1.0 - frac) * invTotal;
        }
        pendSamples_ = 0;
    }
    long long budget_;
    long long processed_{0};
    double stepX_, stepY_, wpp_;
    bool equalityCurve_, notEqual_;
    bool closedBand_{false}; // closed inequality: also accumulate the boundary band
    int bandFloor_{1};       // half-pixel band cells, same floor as the equality regime
    std::vector<double> bandAcc_;
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
        // CLOSED inequalities: every box that reaches half-pixel size scores
        // the boundary band of f=0 for its cell -- INCLUDING boxes the region
        // classify is about to prove uniform, which carry the band's outer
        // feather. Boxes pruned at larger sizes sit >1px from the curve,
        // where the band is zero anyway.
        if (closedBand_ && (b.x1 - b.x0) == bandFloor_) addBandCell(b, ix, iy);
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
        // Equalities march on HALF-pixel cells (2x supersampled extraction):
        // placement error halves, and adjacent detail levels' stroke quality
        // overlaps, so a zoom across a level boundary no longer snaps between
        // a thin and a chunky rendering of the same strands.
        const int floorSize = equalityCurve_ ? std::max(1, pixelSub_ / 2) : 1;
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
    // `varFrac` is the VARIANCE of covFrac as an estimate (0 for proven boxes);
    // it accumulates with the square of the area weight, so finalize() can read
    // a pixel's true RMS coverage error as sqrt(unc)/pixelArea.
    void addUniform(const Box &b, double covFrac, double varFrac)
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
                    unc_[idx] += varFrac * full * full;
                }
        }
        else
        {
            const int px = b.x0 >> K_, py = b.y0 >> K_;
            const size_t idx = static_cast<size_t>(py) * T_ + px;
            const double area = static_cast<double>(sz) * sz;
            acc_[idx] += covFrac * area;
            unc_[idx] += varFrac * area * area;
        }
    }

    // Sample one box (M x M stratified, world-aligned jittered) -> its coverage
    // measure, distributed via addUniform (correct pixel overlap, no double-count).
    // The samples are evaluated as ONE batch (SIMD kernels + amortized dispatch).
    void sampleBoxInto(const Box &b, int M)
    {
        const double x0w = rect_.x0 + b.x0 * stepX_;
        const double y0w = rect_.y0 + b.y0 * stepY_;
        const double bw = (b.x1 - b.x0) * stepX_;
        const double bh = (b.y1 - b.y0) * stepY_;
        const double inv = 1.0 / M;
        double bx[256], by[256];
        int k = 0;
        for (int sy = 0; sy < M; ++sy)
            for (int sx = 0; sx < M; ++sx)
            {
                const double ox = x0w + sx * inv * bw; // stratum world origin
                const double oy = y0w + sy * inv * bh;
                double jx, jy;
                jitter2(ox, oy, jx, jy);
                bx[k] = ox + jx * inv * bw;
                by[k] = oy + jy * inv * bh;
                ++k;
            }
        const int hits = rel_.pointInsideCount(bx, by, k, s_);
        const int total = M * M;
        const double frac = static_cast<double>(hits) / total;
        addUniform(b, frac, frac * (1.0 - frac) / total); // estimate + variance
    }

    // Budget-bailout estimate for an unprocessed box. With sampling on (inequalities),
    // MEASURE it PER PIXEL (M x M each) instead of a flat 0.5 or one blocky per-box
    // value -> the oscillation-dominated frontier renders smooth, like the explicit-1D
    // path, instead of blocky chunks. Equalities keep the flat estimate (their
    // measure-zero curve can't be point-sampled -> the band model handles them).
    void addHalf(const Box &b)
    {
        if (equalityCurve_)
        {
            // A curve is measure-zero: painting unprocessed cells 0.5 gray was a
            // terribly wrong prior (gray slabs in drafts of curve-dense tiles).
            // Estimate EMPTY (full for !=) and let refinement fill the band in.
            addUniform(b, notEqual_ ? 1.0 : 0.0, 0.25);
            return;
        }
        if (floorN_ <= 1)
        {
            addUniform(b, 0.5, 0.25); // legacy flat estimate (worst-case variance)
            return;
        }
        const int sz = b.x1 - b.x0;
        if (sz <= pixelSub_)
        {
            sampleBoxInto(b, bailM_); // sub-pixel or single-pixel box
            return;
        }
        // Multi-pixel box: split to pixel-sized boxes so it is not a flat blocky chunk.
        // Polled for cancel: a large bailed box is tens of ms of sampling otherwise.
        for (int py = b.y0; py < b.y1; py += pixelSub_)
            for (int px = b.x0; px < b.x1; px += pixelSub_)
            {
                if (cancel_.cancelled()) return; // abandoned: never published
                sampleBoxInto(Box{px, py, px + pixelSub_, py + pixelSub_}, bailM_);
            }
    }

    // Floor leaf. For inequalities this is a single sub-pixel cell, resolved by
    // an unbiased center sample. For equalities it is a whole pixel, where the
    // gradient band gives a resolution-independent ~1px anti-aliased line.
    void addFloor(const Box &b, const Interval &ix, const Interval &iy)
    {
        const int px = b.x0 >> K_, py = b.y0 >> K_;
        const size_t idx = static_cast<size_t>(py) * T_ + px;

        if (!equalityCurve_)
        {
            if (floorN_ <= 1)
            {
                // legacy: byte-identical single center sample
                const double cx = rect_.x0 + (b.x0 + 0.5) * stepX_;
                const double cy = rect_.y0 + (b.y0 + 0.5) * stepY_;
                acc_[idx] += rel_.pointInside(cx, cy, s_) ? 1.0 : 0.0;
                unc_[idx] += 1.0;
                return;
            }
            // N x N stratified (world-jittered) samples over the one uncertain
            // sub-cell -> its coverage MEASURE (true-fraction), so sub-pixel
            // oscillation averages to the analytic measure instead of binary 0/1
            // grain. Sub-cell area is 1.0 (sub-cell^2). The samples are DEFERRED
            // into the pending buffer and evaluated as full SIMD batches; the
            // cell's frac/variance land in flushFloorPending(). (Uncertainty =
            // sampling variance, NOT a full unknown cell: a floor cell is as
            // resolved as it gets, so it must read as a settled estimate.)
            const int spc = floorN_ * floorN_;
            if (pendSamples_ + spc > kPendCap) flushFloorPending();
            const double x0w = rect_.x0 + b.x0 * stepX_;
            const double y0w = rect_.y0 + b.y0 * stepY_;
            const double inv = 1.0 / floorN_;
            pendPix_[pendSamples_ / spc] = idx;
            for (int sy = 0; sy < floorN_; ++sy)
                for (int sx = 0; sx < floorN_; ++sx)
                {
                    const double ox = x0w + sx * inv * stepX_;
                    const double oy = y0w + sy * inv * stepY_;
                    double jx, jy;
                    jitter2(ox, oy, jx, jy);
                    pendX_[pendSamples_] = ox + jx * inv * stepX_;
                    pendY_[pendSamples_] = oy + jy * inv * stepY_;
                    ++pendSamples_;
                }
            return;
        }

        // equality: one pixel-sized box (band model; variance is a modeling bound)
        const double cx = rect_.x0 + (b.x0 + b.x1) * 0.5 * stepX_;
        const double cy = rect_.y0 + (b.y0 + b.y1) * 0.5 * stepY_;
        const double area = static_cast<double>(b.x1 - b.x0) * (b.y1 - b.y0);
        Interval val, gvx, gvy;
        rel_.valueAndGrad(ix, iy, s_, val, gvx, gvy);
        if (val.undef)
        {
            // undefined everywhere on the cell: neither = nor != renders here
            unc_[idx] += area * area;
            return;
        }
        // A pole / branch cut touching the cell makes a corner sign change
        // MEANINGLESS: -inf -> +inf is not a zero crossing (y = 1/x used to
        // draw a spurious vertical line down the asymptote). The disc flag is
        // sound, so suppressing band + segments here can never erase a
        // genuinely continuous crossing.
        const bool pole = val.disc;
        double cov;
        if (!pole && gvx.straddlesZero() && gvy.straddlesZero())
        {
            // OSCILLATORY/CRITICAL REGIME: when both gradient components
            // straddle zero over the cell, the band's |f(center)|/|grad mid|
            // distance is meaningless (the interval midpoints collapse toward
            // 0 and the estimate explodes -- dense families rendered as random
            // darkness). Fall back to the interval itself: light the cell iff
            // the enclosure cannot exclude the curve. Dense families saturate
            // to the honest wash; isolated junction/extremum cells light their
            // single pixel.
            cov = val.straddlesZero() ? 1.0 : 0.0;
        }
        else
        {
            cov = pole ? 0.0 : bandCoverage(val, gvx, gvy, cx, cy);
        }
        if (notEqual_) cov = 1.0 - cov;
        acc_[idx] += cov * area;
        unc_[idx] += area * area;
    }

    // Boundary-band cell for a CLOSED inequality (>=, <=): the same certified
    // band model the equality regime uses (pole-suppressed, oscillation-safe),
    // accumulated into a separate plane the presenter draws as the luminous
    // boundary. A >= line IS the = line, plus the fill.
    void addBandCell(const Box &b, const Interval &ix, const Interval &iy)
    {
        const int px = b.x0 >> K_, py = b.y0 >> K_;
        const size_t idx = static_cast<size_t>(py) * T_ + px;
        const double cx = rect_.x0 + (b.x0 + b.x1) * 0.5 * stepX_;
        const double cy = rect_.y0 + (b.y0 + b.y1) * 0.5 * stepY_;
        const double area = static_cast<double>(b.x1 - b.x0) * (b.y1 - b.y0);
        Interval val, gvx, gvy;
        rel_.valueAndGrad(ix, iy, s_, val, gvx, gvy);
        if (val.undef || val.disc) return; // undefined / pole-crossing: no line
        double cov;
        if (gvx.straddlesZero() && gvy.straddlesZero())
            cov = val.straddlesZero() ? 1.0 : 0.0; // oscillatory: enclosure test
        else
            cov = bandCoverage(val, gvx, gvy, cx, cy);
        bandAcc_[idx] += cov * area;
    }

    // Coverage of a sub-cell by the ~1px-wide curve band of f=0 (the caller
    // already evaluated the cell's value/gradient intervals).
    double bandCoverage(const Interval &val, const Interval &gx, const Interval &gy, double cx,
                        double cy)
    {
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
        flushFloorPending(); // deferred floor cells land before the readout
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
            // unc_ holds summed VARIANCE (area^2-weighted): RMS error = sqrt/area.
            worst = std::max(worst, static_cast<float>(std::sqrt(unc_[i]) / perPixel));
        }
        tile.worstUncertainty = worst;
        if (closedBand_)
        {
            tile.band.resize(tile.alpha.size());
            for (size_t i = 0; i < tile.band.size(); ++i)
                tile.band[i] =
                    static_cast<float>(std::clamp(bandAcc_[i] / perPixel, 0.0, 1.0));
        }
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
// Returns false if a sample was non-finite (overflow / asymptote -> the fast point
// sampler can't be trusted here); the caller then falls back to the sound interval
// subdivision, which correctly distinguishes "bounded but unknown" from "undefined".
bool solveExplicit1D(const Relation &rel, const WorldRect &rect, const SolveParams &params,
                     EvalScratch &scratch, const CancelToken &cancel, CoverageTile &tile)
{
    const int T = params.tilePx;
    const Program &G = *rel.explicitG();
    const CmpOp op = rel.explicitOp();
    const bool greater = (op == CmpOp::Greater || op == CmpOp::GreaterEq);
    const bool wantBand = (op == CmpOp::GreaterEq || op == CmpOp::LessEq);
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

    tile.width = tile.height = T;
    tile.subBits = params.subBits;
    tile.converged = true;
    tile.alpha.assign(static_cast<size_t>(T) * T, 0.0f);
    if (wantBand) tile.band.assign(static_cast<size_t>(T) * T, 0.0f);

    std::vector<double> colBand; // per-column band difference array (closed only)
    std::vector<double> g;
    std::vector<double> prefix;
    g.reserve(S);
    prefix.reserve(S + 1);
    std::vector<double> ws(static_cast<size_t>(S));
    std::vector<double> zs(static_cast<size_t>(S), 0.0);
    std::vector<double> vals(static_cast<size_t>(S));

    for (int o = 0; o < T; ++o)
    {
        if (cancel.cancelled()) // per column: a column is up to ~8k evals (~0.2ms)
        {
            tile.converged = false;
            return true; // cancelled (not unreliable): caller handles the cancel token
        }
        const double wa = wOrigin + o * wStepPix;
        for (int k = 0; k < S; ++k) ws[static_cast<size_t>(k)] = wa + (k + 0.5) * wStepPix / S;
        if (yExplicit)
            G.evalPointBatch(ws.data(), zs.data(), vals.data(), S, scratch.sb);
        else
            G.evalPointBatch(zs.data(), ws.data(), vals.data(), S, scratch.sb);
        g.clear();
        for (int k = 0; k < S; ++k)
        {
            const double val = vals[static_cast<size_t>(k)];
            // A non-finite sample means g(x) overflowed or hit an asymptote here -- the
            // point sampler can't measure this column. Bail so solveTile uses the sound
            // interval subdivision instead (which bounds e.g. sin by [-1,1] correctly).
            if (!std::isfinite(val)) return false;
            g.push_back(val);
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

        if (wantBand)
        {
            // Boundary band for the CLOSED inequality, scattered per sample in
            // O(S+T) with a difference array: sample k contributes its vertical
            // slice [g-h, g+h], h slope-corrected so the PERPENDICULAR width
            // matches the 2-D band model (a steep line must not thin out).
            // Summed slices integrate to the band's true area measure, so a
            // sub-pixel oscillation wall saturates honestly to a luminous slab
            // while a smooth stretch stays a ~1px line.
            colBand.assign(static_cast<size_t>(T) + 1, 0.0);
            const double invS = 1.0 / S;
            const double sampleDw = wStepPix / S;
            for (int k = 0; k < S; ++k)
            {
                const int km = k > 0 ? k - 1 : 0;
                const int kp = k < S - 1 ? k + 1 : S - 1;
                const double dgdw = (vals[static_cast<size_t>(kp)] -
                                     vals[static_cast<size_t>(km)]) /
                                    ((kp - km) * sampleDw);
                const double mPix = dgdw * wStepPix / vStepPix; // pixel-space slope
                const double h = 0.6 * std::sqrt(1.0 + mPix * mPix) * vStepPix;
                double lo = (vals[static_cast<size_t>(k)] - h - vOrigin) / vStepPix;
                double hi = (vals[static_cast<size_t>(k)] + h - vOrigin) / vStepPix;
                if (hi <= 0.0 || lo >= static_cast<double>(T)) continue;
                lo = std::max(lo, 0.0);
                hi = std::min(hi, static_cast<double>(T));
                const int p0 = std::min(static_cast<int>(lo), T - 1);
                const int p1 = std::min(static_cast<int>(hi), T - 1);
                if (p0 == p1)
                {
                    const double v = (hi - lo) * invS;
                    colBand[p0] += v;
                    colBand[p0 + 1] -= v;
                    continue;
                }
                const double part0 = (p0 + 1 - lo) * invS;
                const double part1 = (hi - p1) * invS;
                colBand[p0] += part0;
                colBand[p0 + 1] -= part0;
                colBand[p1] += part1;
                colBand[p1 + 1] -= part1;
                if (p1 - p0 >= 2) // full interior pixels
                {
                    colBand[p0 + 1] += invS;
                    colBand[p1] -= invS;
                }
            }
            double run = 0.0;
            for (int i = 0; i < T; ++i)
            {
                run += colBand[static_cast<size_t>(i)];
                const size_t idx = yExplicit ? static_cast<size_t>(i) * T + o
                                             : static_cast<size_t>(o) * T + i;
                tile.band[idx] = static_cast<float>(std::clamp(run, 0.0, 1.0));
            }
        }
    }
    return true;
}
}

CoverageTile solveTile(const Relation &rel, const WorldRect &rect, const SolveParams &params,
                       EvalScratch &scratch, const CancelToken &cancel)
{
    // Fast, exact-measure path for explicit-1D inequalities (v <op> g(w)). Falls
    // back to the sound subdivision when g(x) is non-finite (overflow/asymptote),
    // which the point sampler can't measure but interval arithmetic bounds correctly.
    if (params.analytic && rel.explicit1D() && !rel.isEquality())
    {
        CoverageTile t;
        if (solveExplicit1D(rel, rect, params, scratch, cancel, t)) return t;
    }
    TileSolver solver(rel, rect, params, scratch, cancel);
    return solver.run();
}

NodeClass classifyRegion(const Relation &rel, const WorldRect &rect, EvalScratch &scratch,
                         const CancelToken &cancel, long long splitBudget)
{
    struct DBox
    {
        double x0, y0, x1, y1;
    };
    std::vector<DBox> stack;
    stack.reserve(64);
    stack.push_back({rect.x0, rect.y0, rect.x1, rect.y1});
    // A boundary still uncertain below this size means the region is genuinely
    // mixed (not uniform). Bounded depth -> cheap, and Mixed is always the safe
    // answer, so a too-shallow floor only loses greediness, never soundness.
    const double minW = rect.width() / 256.0;
    const double minH = rect.height() / 256.0;
    bool sawTrue = false, sawFalse = false;
    long long processed = 0;

    while (!stack.empty())
    {
        if (++processed > splitBudget) return NodeClass::Mixed; // unproven -> safe
        if ((processed & 255) == 0 && cancel.cancelled()) return NodeClass::Mixed;
        const DBox b = stack.back();
        stack.pop_back();
        const Sign s = rel.classifyBox(Interval{b.x0, b.x1}, Interval{b.y0, b.y1}, scratch);
        if (s == Sign::AllTrue)
        {
            sawTrue = true;
            if (sawFalse) return NodeClass::Mixed;
        }
        else if (s == Sign::AllFalse || s == Sign::Undefined)
        {
            sawFalse = true; // undefined renders nothing == false
            if (sawTrue) return NodeClass::Mixed;
        }
        else // Uncertain: a boundary or discontinuity is inside this box
        {
            if ((b.x1 - b.x0) <= minW || (b.y1 - b.y0) <= minH) return NodeClass::Mixed;
            const double mx = 0.5 * (b.x0 + b.x1);
            const double my = 0.5 * (b.y0 + b.y1);
            stack.push_back({b.x0, b.y0, mx, my});
            stack.push_back({mx, b.y0, b.x1, my});
            stack.push_back({b.x0, my, mx, b.y1});
            stack.push_back({mx, my, b.x1, b.y1});
        }
    }
    if (sawTrue && !sawFalse) return NodeClass::UniformTrue;
    if (sawFalse && !sawTrue) return NodeClass::UniformFalse;
    return NodeClass::Mixed;
}
}
