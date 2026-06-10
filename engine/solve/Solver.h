#ifndef GXR_SOLVE_SOLVER_H
#define GXR_SOLVE_SOLVER_H

#include "Coverage.h"
#include "expr/Relation.h"
#include "tile/Geometry.h"

#include <atomic>

namespace gxr
{
struct SolveParams
{
    int tilePx{64};        // output resolution per axis (power of two)
    int subBits{4};        // sub-pixel floor depth: floor cell = pixel / 2^subBits
    long long boxBudget{400000}; // max boxes processed before bailing to estimate
    bool analytic{true};   // use analytic accelerators where structure permits
    int floorSamples{2};   // per-axis sub-samples (N) for an UNCERTAIN floor/bailout
                           // cell: N*N point evals -> the cell's coverage MEASURE,
                           // so sub-pixel oscillation in general 2-D relations renders
                           // smooth (like the explicit-1D path) instead of grainy.
                           // 1 => legacy single center sample; 2 = smooth & cheap.
    int bailSamples{16};   // per-axis samples (M) for a pixel the budget bailout
                           // estimates: M*M point evals -> pixel stderr ~ 0.5/M.
};

// The progressive-refinement ladder for a DETAIL tile: pass 0 is a cheap first
// paint (a few ms even on a pathological tile), later passes sharpen, and the
// final pass is byte-identical to the legacy single-shot fine solve. One shared
// definition so the engine, tests and tools can never drift apart.
inline constexpr int kMaxRefinePass = 3;
[[nodiscard]] inline SolveParams refinePassParams(int tilePx, int pass)
{
    switch (pass)
    {
    case 0: return SolveParams{tilePx, 1, 12'000, true, 2, 4};   // first paint, ~eps 1/8
    case 1: return SolveParams{tilePx, 2, 50'000, true, 2, 8};   // ~eps 1/16
    case 2: return SolveParams{tilePx, 3, 100'000, true, 2, 12}; // ~eps 1/24
    default: return SolveParams{tilePx, 4, 200'000, true, 2, 16}; // == legacy fine
    }
}

// Cooperative cancellation: `flag` is the epoch cancel (relation changed), `abort`
// the per-job viewport abandon (this tile's output is no longer drawable). Both
// mean "stop now, do not publish". Polled between subdivision batches AND inside
// the sampling loops, so even a pathological tile frees its worker in well under
// a millisecond.
struct CancelToken
{
    const std::atomic<bool> *flag{nullptr};
    const std::atomic<bool> *abort{nullptr};
    [[nodiscard]] bool cancelled() const
    {
        return (flag && flag->load(std::memory_order_relaxed)) ||
               (abort && abort->load(std::memory_order_relaxed));
    }
};

// Solve one tile: produce per-pixel coverage of `rel` over `rect`.
// Pure and deterministic given (rel, rect, params) -> identical tiles cache &
// stay temporally stable. `scratch` is reused per worker thread.
[[nodiscard]] CoverageTile solveTile(const Relation &rel, const WorldRect &rect,
                                     const SolveParams &params, EvalScratch &scratch,
                                     const CancelToken &cancel = {});

// Classify a whole region for the greedy quadtree. Returns UniformTrue/UniformFalse
// ONLY when the interval solver proves every sub-box has that one sign (sound at
// any zoom); returns Mixed whenever a boundary, discontinuity, or unresolved box
// is found, or the split budget is hit (safe default). Cheap: uniform regions
// prove in a few boxes, boundaries early-out as soon as both signs appear.
[[nodiscard]] NodeClass classifyRegion(const Relation &rel, const WorldRect &rect,
                                       EvalScratch &scratch, const CancelToken &cancel = {},
                                       long long splitBudget = 600);
}

#endif // GXR_SOLVE_SOLVER_H
