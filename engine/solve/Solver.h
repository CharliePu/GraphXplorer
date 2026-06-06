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
};

// Cooperative cancellation: checked between BFS levels so stale work abandons cheaply.
struct CancelToken
{
    const std::atomic<bool> *flag{nullptr};
    [[nodiscard]] bool cancelled() const { return flag && flag->load(std::memory_order_relaxed); }
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
