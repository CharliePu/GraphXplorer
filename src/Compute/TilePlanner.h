#ifndef TILEPLANNER_H
#define TILEPLANNER_H

#include <chrono>
#include <vector>

#include "TileJob.h"
#include "../Tile/TileCache.h"
#include "../Tile/TileMath.h"
#include "../Util/Contracts.h"

class ThreadPool;

namespace gx
{
struct TilePlanStats
{
    size_t seedTiles{0};
    size_t snapshotRecords{0};
    size_t visitedTiles{0};
    size_t candidatesDiscovered{0};
    size_t committedCandidates{0};
    size_t resultChunks{0};
    size_t workerCount{0};
    size_t idleWorkersAtStart{0};
    size_t offloadedTasks{0};
    size_t inlineTasks{0};
    std::chrono::microseconds discoveryTime{0};
    std::chrono::microseconds commitTime{0};
    std::chrono::microseconds totalTime{0};
    bool parallelEnabled{false};
};

struct TilePlan
{
    std::vector<TileJob> jobs{};
    std::vector<TileKey> erasedShadowedTiles{};
    TilePlanStats stats{};
};

class TilePlanner
{
public:
    [[nodiscard]] TilePlan plan(const ViewportRequest &request,
                                TileCache &tileCache,
                                const TilePlanBudget &budget = TilePlanBudget{},
                                int maxSeedCells = 4,
                                int refinementDepth = DefaultRefinementDepth,
                                ThreadPool *workers = nullptr) const;
};
}

#endif // TILEPLANNER_H
