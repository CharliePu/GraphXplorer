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
}

#endif // GXR_SOLVE_SOLVER_H
