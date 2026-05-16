#ifndef TILEPLANNER_H
#define TILEPLANNER_H

#include <vector>

#include "TileJob.h"
#include "../Tile/TileCache.h"
#include "../Util/Contracts.h"

namespace gx
{
struct TilePlan
{
    std::vector<TileJob> jobs{};
    std::vector<TileKey> erasedShadowedTiles{};
};

class TilePlanner
{
public:
    [[nodiscard]] TilePlan plan(const ViewportRequest &request,
                                TileCache &tileCache,
                                const TilePlanBudget &budget = TilePlanBudget{},
                                int maxSeedCells = 4) const;

private:
    struct BudgetState
    {
        int interval{0};
        int raster{0};
    };

    static void visitAuthority(const ViewportRequest &request,
                               TileCache &tileCache,
                               const TileKey &key,
                               int leafLevel,
                               TilePlan &plan,
                               BudgetState &budget);
    static void enqueueClassifyIfIdle(TilePlan &plan,
                                      BudgetState &budget,
                                      const ViewportRequest &request,
                                      TileCache &tileCache,
                                      const TileKey &key);
    static void enqueueRasterIfIdle(TilePlan &plan,
                                    BudgetState &budget,
                                    const ViewportRequest &request,
                                    TileCache &tileCache,
                                    const TileKey &key,
                                    const TileRecord &record);
    [[nodiscard]] static int priorityFor(const ViewportRequest &request, const TileKey &key);
};
}

#endif // TILEPLANNER_H
