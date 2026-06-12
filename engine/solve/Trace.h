#ifndef GXR_SOLVE_TRACE_H
#define GXR_SOLVE_TRACE_H

#include "expr/Relation.h"

namespace gxr
{
// Cursor trace: the point on the curve f=0 (an equality, or the boundary of
// a closed inequality -- same residual) nearest a cursor position. Newton
// glide along the gradient (the band model's own distance geometry) follows
// the curve in any orientation; axis brackets + bisection catch what Newton
// can't (oscillation, kinks). The hit is then CERTIFIED by interval
// arithmetic: on a box around it, one gradient component is bounded off
// zero and the two edge slabs across that axis carry opposite PROVEN signs,
// so a crossing must lie inside. `certified` distinguishes proof from a
// merely-converged (heuristic) hit. Cheap: a few dozen evaluations.
struct TraceHit
{
    bool traced{false};
    bool certified{false};
    double x{0.0};
    double y{0.0};
};

// `reachPx`: how far (screen px) the search may wander from the cursor.
// Hover uses the default; dragging a pinned point along the curve passes a
// generous reach so the point keeps following a distant cursor.
TraceHit traceCurve(const Relation &rel, double cursorX, double cursorY, double wppX,
                    double wppY, EvalScratch &scratch, double reachPx = 26.0);
}

#endif // GXR_SOLVE_TRACE_H
