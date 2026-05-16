#ifndef UPLOADPLANNER_H
#define UPLOADPLANNER_H

#include <span>

#include "../Tile/TileCache.h"
#include "../Util/Contracts.h"

namespace gx
{
class UploadPlanner
{
public:
    [[nodiscard]] UploadPlan plan(std::span<const TileRecord> records, const UploadBudget &budget) const;
    [[nodiscard]] UploadPlan planVisible(std::span<const DisplayTile> tiles, const UploadBudget &budget) const;
};
}

#endif // UPLOADPLANNER_H
