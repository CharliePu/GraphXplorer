#ifndef GXR_SOLVE_COVERAGE_H
#define GXR_SOLVE_COVERAGE_H

#include <cstdint>
#include <memory>
#include <vector>

namespace gxr
{
// A quadtree node's proven classification. UniformTrue/UniformFalse are exact at
// ANY zoom (greedy tiles); Mixed must be subdivided / rastered. A node is marked
// Uniform ONLY when the interval solver PROVES it (never on a sample or a budget
// bail), so a greedy tile never renders a wrong region.
enum class NodeClass : uint8_t
{
    Unknown,
    UniformTrue,
    UniformFalse,
    Mixed,
};

// A solved tile: per-pixel coverage in [0,1] (fraction of the pixel where the
// relation holds), row-major, width*height. Immutable once published; the main
// thread reads it through a shared_ptr<const CoverageTile>.
struct CoverageTile
{
    int width{0};
    int height{0};
    std::vector<float> alpha; // size width*height, coverage in [0,1]

    int subBits{0};            // sub-pixel refinement depth used
    bool converged{false};     // solved fully within budget (no estimate band left)
    float worstUncertainty{0}; // max per-pixel uncertain area fraction (0 = exact)
    uint64_t payloadId{0};     // monotonic id for "is this newer" comparisons

    // Equality relations only: marching-squares line segments extracted from
    // the soundly-isolated boundary cells, as (x0,y0,x1,y1) quadruples in
    // TILE-LOCAL [0,1] coordinates (origin at the tile rect's min corner).
    // Vector data: the presenter strokes these at constant screen width, so
    // curves stay crisp through zoom while re-solves are pending. The band
    // raster in `alpha` stays as the fallback (stand-ins, bailed cells).
    std::vector<float> segs;
    // Stroke weight in [0,1]: 1 = sparse tile, strokes fully replace the band;
    // ramping to 0 as the curve family approaches the saturation density where
    // strokes hand off to the raster. The presenter blends band + faded
    // strokes in the ramp, so the regime switch has no tile-blocky seam.
    float strokeAlpha{1.0f};

    [[nodiscard]] float at(int x, int y) const { return alpha[static_cast<size_t>(y) * width + x]; }
};

using CoverageTilePtr = std::shared_ptr<const CoverageTile>;
}

#endif // GXR_SOLVE_COVERAGE_H
