#ifndef INEQUALITYTILEREFINER_H
#define INEQUALITYTILEREFINER_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "ComputeBackend.h"

namespace gx
{
inline constexpr int DefaultRasterProofExtraDepth = 4;

struct InequalityTileRefinementOptions
{
    uint32_t pixelsPerAxis{RasterTexturePixels};
    int subpixelExtraDepth{DefaultRasterProofExtraDepth};
    size_t maxVisitedNodes{500000};
    std::optional<Rect> rootBounds{};
    std::function<bool()> cancelled{};
};

struct InequalityTileRefinementResult
{
    bool ok{true};
    std::string message{};
    RegionOutput region{};
    size_t visitedNodes{0};
    size_t unknownPixels{0};
};

[[nodiscard]] InequalityTileRefinementResult refineInequalityTile(
    const CompiledFormula &formula,
    const TileKey &previewKey,
    const InequalityTileRefinementOptions &options);
}

#endif // INEQUALITYTILEREFINER_H
