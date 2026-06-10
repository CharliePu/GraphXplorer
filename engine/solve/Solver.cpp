#include "Solver.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
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
    bool useCentered_{true};
    long long centeredAttempts_{0};
    long long centeredWins_{0};
    std::vector<double> acc_; // covered sub-cell area per pixel
    std::vector<double> unc_; // boundary/estimated sub-cell count per pixel
    std::vector<float> segs_; // equality: marching-squares segments (tile-local)

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
        double cov = pole ? 0.0 : bandCoverage(val, gvx, gvy, cx, cy);
        if (notEqual_) cov = 1.0 - cov;
        acc_[idx] += cov * area;
        unc_[idx] += area * area;
        if (!notEqual_ && !pole) marchCell(b, cx, cy); // extract the curve's segments
    }

    // Marching squares over one boundary pixel cell of an EQUALITY curve. The
    // interval subdivision already PROVED a sign change may cross this cell, so
    // segment detection inherits that soundness; corner signs + linear
    // interpolation place the crossing sub-pixel-accurately, and the center
    // sample disambiguates the two saddle cases. Segments are stored in
    // tile-local [0,1] coordinates (vector data: crisp at any zoom).
    void marchCell(const Box &b, double cx, double cy)
    {
        const double x0w = rect_.x0 + b.x0 * stepX_;
        const double x1w = rect_.x0 + b.x1 * stepX_;
        const double y0w = rect_.y0 + b.y0 * stepY_;
        const double y1w = rect_.y0 + b.y1 * stepY_;
        const double f0 = rel_.fValue(x0w, y0w, s_); // corner order: BL, BR, TR, TL
        const double f1 = rel_.fValue(x1w, y0w, s_);
        const double f2 = rel_.fValue(x1w, y1w, s_);
        const double f3 = rel_.fValue(x0w, y1w, s_);
        if (!std::isfinite(f0) || !std::isfinite(f1) || !std::isfinite(f2) || !std::isfinite(f3))
            return; // undefined corner: the band raster covers this cell

        const int code = (f0 < 0.0) | ((f1 < 0.0) << 1) | ((f2 < 0.0) << 2) | ((f3 < 0.0) << 3);
        if (code == 0 || code == 15) return; // no corner sign change in this cell

        // crossing parameter on an edge from value a to value b
        const auto cross = [](double a, double bb) {
            const double d = a - bb;
            return d == 0.0 ? 0.5 : std::clamp(a / d, 0.0, 1.0);
        };
        // edge points in CELL-local [0,1]^2: e0 bottom, e1 right, e2 top, e3 left
        const double e0x = cross(f0, f1), e1y = cross(f1, f2);
        const double e2x = 1.0 - cross(f2, f3), e3y = 1.0 - cross(f3, f0);
        const double ex[4] = {e0x, 1.0, e2x, 0.0};
        const double ey[4] = {0.0, e1y, 1.0, e3y};

        const double invT = 1.0 / T_;
        const double px = static_cast<double>(b.x0 >> K_);
        const double py = static_cast<double>(b.y0 >> K_);
        const auto emitSeg = [&](int ea, int eb) {
            segs_.push_back(static_cast<float>((px + ex[ea]) * invT));
            segs_.push_back(static_cast<float>((py + ey[ea]) * invT));
            segs_.push_back(static_cast<float>((px + ex[eb]) * invT));
            segs_.push_back(static_cast<float>((py + ey[eb]) * invT));
        };

        switch (code)
        {
        case 1: case 14: emitSeg(3, 0); break;
        case 2: case 13: emitSeg(0, 1); break;
        case 3: case 12: emitSeg(3, 1); break;
        case 4: case 11: emitSeg(1, 2); break;
        case 6: case 9: emitSeg(0, 2); break;
        case 7: case 8: emitSeg(2, 3); break;
        case 5: case 10: // saddle: the center sample picks the pairing
        {
            const double fc = rel_.fValue(cx, cy, s_);
            const bool cNeg = std::isfinite(fc) && fc < 0.0;
            const bool sameAsBL = (code == 5) == cNeg;
            if (sameAsBL)
            {
                emitSeg(0, 1);
                emitSeg(2, 3);
            }
            else
            {
                emitSeg(3, 0);
                emitSeg(1, 2);
            }
            break;
        }
        default: break;
        }
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

    // Marching squares at pixel cells emits one tiny segment per boundary cell
    // -- a curve-dense tile easily carries hundreds, and the presenter pays for
    // every one each frame. Chain segments into polylines at shared endpoints
    // (stopping at junctions, so web crossings survive) and simplify each chain
    // with Douglas-Peucker at a sub-pixel tolerance: smooth runs collapse
    // 10-30x with no visible change. Deterministic (order- and value-stable).
    void simplifySegs()
    {
        const size_t nSegs = segs_.size() / 4;
        if (nSegs < 8) return;
        constexpr double kTol = 0.0008; // tile-local: ~0.05 px at 64 px

        struct Pt
        {
            float x, y;
        };
        const auto keyOf = [](float x, float y) {
            const auto qx = static_cast<uint64_t>(static_cast<int64_t>(x * 16384.0f + 0.5f));
            const auto qy = static_cast<uint64_t>(static_cast<int64_t>(y * 16384.0f + 0.5f));
            return (qx << 32) ^ qy;
        };
        // endpoint -> incident segment indices (degree > 2 = junction)
        std::unordered_map<uint64_t, std::vector<uint32_t>> ends;
        ends.reserve(nSegs * 2);
        for (uint32_t s = 0; s < nSegs; ++s)
        {
            ends[keyOf(segs_[4 * s], segs_[4 * s + 1])].push_back(s);
            ends[keyOf(segs_[4 * s + 2], segs_[4 * s + 3])].push_back(s);
        }
        std::vector<char> used(nSegs, 0);
        std::vector<Pt> chain;
        std::vector<float> out;
        out.reserve(segs_.size() / 4);

        const auto emitChain = [&] {
            // iterative Douglas-Peucker over `chain`
            if (chain.size() < 2) return;
            std::vector<char> keep(chain.size(), 0);
            keep.front() = keep.back() = 1;
            std::vector<std::pair<size_t, size_t>> stack{{0, chain.size() - 1}};
            while (!stack.empty())
            {
                const auto [a, b] = stack.back();
                stack.pop_back();
                if (b <= a + 1) continue;
                const double ax = chain[a].x, ay = chain[a].y;
                const double dx = chain[b].x - ax, dy = chain[b].y - ay;
                const double len2 = dx * dx + dy * dy;
                double worst = -1.0;
                size_t wi = a;
                for (size_t i = a + 1; i < b; ++i)
                {
                    const double px = chain[i].x - ax, py = chain[i].y - ay;
                    double d2;
                    if (len2 <= 1e-24)
                        d2 = px * px + py * py;
                    else
                    {
                        const double t = std::clamp((px * dx + py * dy) / len2, 0.0, 1.0);
                        const double ex = px - t * dx, ey = py - t * dy;
                        d2 = ex * ex + ey * ey;
                    }
                    if (d2 > worst)
                    {
                        worst = d2;
                        wi = i;
                    }
                }
                if (worst > kTol * kTol)
                {
                    keep[wi] = 1;
                    stack.push_back({a, wi});
                    stack.push_back({wi, b});
                }
            }
            const Pt *prev = nullptr;
            for (size_t i = 0; i < chain.size(); ++i)
            {
                if (!keep[i]) continue;
                if (prev)
                    out.insert(out.end(), {prev->x, prev->y, chain[i].x, chain[i].y});
                prev = &chain[i];
            }
        };

        const auto degree = [&](float x, float y) -> size_t {
            const auto it = ends.find(keyOf(x, y));
            return it == ends.end() ? 0 : it->second.size();
        };
        const auto walk = [&](uint32_t s0, bool fromFirstEnd) {
            chain.clear();
            uint32_t s = s0;
            float cx, cy; // walking toward this endpoint
            if (fromFirstEnd)
            {
                chain.push_back({segs_[4 * s + 2], segs_[4 * s + 3]});
                cx = segs_[4 * s];
                cy = segs_[4 * s + 1];
            }
            else
            {
                chain.push_back({segs_[4 * s], segs_[4 * s + 1]});
                cx = segs_[4 * s + 2];
                cy = segs_[4 * s + 3];
            }
            chain.push_back({cx, cy});
            used[s] = 1;
            for (;;)
            {
                const auto it = ends.find(keyOf(cx, cy));
                if (it == ends.end() || it->second.size() != 2) break; // junction/end
                uint32_t nxt = it->second[0] == s ? it->second[1] : it->second[0];
                if (used[nxt]) break;
                const bool atFirst =
                    keyOf(segs_[4 * nxt], segs_[4 * nxt + 1]) == keyOf(cx, cy);
                cx = atFirst ? segs_[4 * nxt + 2] : segs_[4 * nxt];
                cy = atFirst ? segs_[4 * nxt + 3] : segs_[4 * nxt + 1];
                chain.push_back({cx, cy});
                used[nxt] = 1;
                s = nxt;
            }
        };

        // start chains at junctions/loose ends first, then mop up loops
        for (uint32_t s = 0; s < nSegs; ++s)
        {
            if (used[s]) continue;
            const bool firstOpen = degree(segs_[4 * s], segs_[4 * s + 1]) != 2;
            const bool secondOpen = degree(segs_[4 * s + 2], segs_[4 * s + 3]) != 2;
            if (!firstOpen && !secondOpen) continue;
            walk(s, firstOpen);
            emitChain();
        }
        for (uint32_t s = 0; s < nSegs; ++s)
        {
            if (used[s]) continue;
            walk(s, true); // closed loop: start anywhere
            emitChain();
        }
        segs_ = std::move(out);
    }

    CoverageTile finalize(bool converged)
    {
        flushFloorPending(); // deferred floor cells land before the readout
        if (equalityCurve_) simplifySegs();
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
        tile.segs = std::move(segs_);
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
