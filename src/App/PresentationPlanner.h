#ifndef PRESENTATIONPLANNER_H
#define PRESENTATIONPLANNER_H

#include <functional>
#include <vector>

#include "../Compute/VisualCoverBuilder.h"
#include "../Render/UploadPlanner.h"
#include "../Util/Contracts.h"

namespace gx
{
struct PresentationPlan
{
    std::vector<DisplayTile> displayTiles{};
    std::vector<DisplayTile> preloadTiles{};
    std::vector<RegionImageRef> visibleRegions{};
    UploadPlan uploadPlan{};
    std::vector<DisplayTile> committedTiles{};
};

struct PresentationPlanRequest
{
    const ViewportRequest &viewport;
    const TileCache &tileCache;
    const CommittedVisualFrame *previous{nullptr};
    int maxSeedCells{4};
    int refinementDepth{DefaultRefinementDepth};
    UploadBudget uploadBudget{};
};

class PresentationPlanner
{
public:
    using RegionSliceResolver = std::function<TextureSlice(const RegionImageRef &)>;

    [[nodiscard]] PresentationPlan plan(const PresentationPlanRequest &request,
                                        const RegionSliceResolver &regionSliceFor) const;

private:
    [[nodiscard]] static bool presentableDisplayTile(const DisplayTile &tile);

    VisualCoverBuilder visualCoverBuilder{};
    UploadPlanner uploadPlanner{};
};
}

#endif // PRESENTATIONPLANNER_H
