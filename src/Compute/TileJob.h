#ifndef TILEJOB_H
#define TILEJOB_H

#include <cstdint>

#include "../Util/Contracts.h"

namespace gx
{
enum class JobKind
{
    ClassifyInterval,
    RasterizeRegion
};

enum class WorkClass
{
    VisibleNow,
    VisibleRefinement
};

struct JobDependencyMask
{
    bool interval{false};
    bool operator==(const JobDependencyMask &) const = default;
};

struct TileJob
{
    JobKind kind{JobKind::ClassifyInterval};
    WorkClass workClass{WorkClass::VisibleNow};
    TileKey key{};
    int priority{0};
    JobDependencyMask dependencies{};
    bool operator==(const TileJob &) const = default;
};

struct TilePlanBudget
{
    int maxIntervalJobsPerFrame{256};
    int maxRasterJobsPerFrame{64};
};
}

#endif // TILEJOB_H
