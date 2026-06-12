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
    // CLOSED inequalities (>=, <=) only: per-pixel coverage of the ~1px
    // boundary band of f=0, computed by the same certified band model the
    // equality regime uses. Empty for every other relation kind. The line is
    // carried as its OWN plane because reconstructing it from `alpha` starves
    // wherever the boundary hugs a tile edge (y >= 0 lies between two tiles'
    // rasters, so neither holds a 0/1 transition).
    std::vector<float> band;

    int subBits{0};            // sub-pixel refinement depth used
    bool converged{false};     // solved fully within budget (no estimate band left)
    float worstUncertainty{0}; // max per-pixel uncertain area fraction (0 = exact)
    uint64_t payloadId{0};     // monotonic id for "is this newer" comparisons

    [[nodiscard]] float at(int x, int y) const { return alpha[static_cast<size_t>(y) * width + x]; }
};

using CoverageTilePtr = std::shared_ptr<const CoverageTile>;
}

#endif // GXR_SOLVE_COVERAGE_H
