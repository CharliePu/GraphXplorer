#ifndef GXR_SOLVE_COVERAGE_H
#define GXR_SOLVE_COVERAGE_H

#include <cstdint>
#include <memory>
#include <vector>

namespace gxr
{
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

    [[nodiscard]] float at(int x, int y) const { return alpha[static_cast<size_t>(y) * width + x]; }
};

using CoverageTilePtr = std::shared_ptr<const CoverageTile>;
}

#endif // GXR_SOLVE_COVERAGE_H
