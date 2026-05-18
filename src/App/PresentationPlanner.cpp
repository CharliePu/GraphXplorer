#include "PresentationPlanner.h"

#include <algorithm>
#include <utility>

namespace gx
{
PresentationPlan PresentationPlanner::plan(const PresentationPlanRequest &request,
                                           const RegionSliceResolver &regionSliceFor) const
{
    auto visualFrame = visualCoverBuilder.build(
        request.viewport,
        request.tileCache,
        request.previous,
        request.maxSeedCells,
        request.refinementDepth,
        [&regionSliceFor](const RegionImageRef &ref)
        {
            return regionSliceFor && regionSliceFor(ref).textureId != 0;
        });

    PresentationPlan result;
    result.displayTiles = std::move(visualFrame.tiles);
    result.preloadTiles = std::move(visualFrame.preloadTiles);
    result.visibleRegions.reserve(result.displayTiles.size());
    for (auto &tile : result.displayTiles)
    {
        if (!tile.cpuRegion)
        {
            continue;
        }

        tile.gpuSlice = regionSliceFor ? regionSliceFor(*tile.cpuRegion) : TextureSlice{};
        result.visibleRegions.push_back(*tile.cpuRegion);
    }

    auto uploadCandidates = result.displayTiles;
    uploadCandidates.insert(uploadCandidates.end(), result.preloadTiles.begin(), result.preloadTiles.end());
    result.uploadPlan = uploadPlanner.planVisible(uploadCandidates, request.uploadBudget);

    result.committedTiles.reserve(result.displayTiles.size());
    for (const auto &tile : result.displayTiles)
    {
        if (presentableDisplayTile(tile))
        {
            result.committedTiles.push_back(tile);
        }
    }

    return result;
}

bool PresentationPlanner::presentableDisplayTile(const DisplayTile &tile)
{
    switch (tile.visualState)
    {
    case TileVisualState::UniformTrue:
    case TileVisualState::UniformFalse:
        return true;
    case TileVisualState::MixedRegion:
        return tile.cpuRegion.has_value() && tile.gpuSlice.textureId != 0;
    default:
        return false;
    }
}
}
