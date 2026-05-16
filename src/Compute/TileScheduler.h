#ifndef TILESCHEDULER_H
#define TILESCHEDULER_H

#include <chrono>
#include <vector>

#include "../Util/Contracts.h"

namespace gx
{
class TileCache;

enum class JobKind
{
    ClassifyInterval,
    RasterizeRegion,
    ExtractContour,
    StageUpload,
    PublishDelta
};

enum class WorkClass
{
    VisibleNow,
    VisibleRefinement,
    NearViewportPrefetch,
    CacheWarmup,
    Upload,
    MaintenanceEviction
};

struct JobDependencyMask
{
    bool interval{false};
    bool region{false};
    bool contour{false};
    bool upload{false};
    bool operator==(const JobDependencyMask &) const = default;
};

struct TileJob
{
    JobKind kind{JobKind::ClassifyInterval};
    WorkClass workClass{WorkClass::VisibleNow};
    TileKey key{};
    uint64_t requestId{0};
    uint64_t generation{0};
    int priority{0};
    JobDependencyMask dependencies{};
    bool operator==(const TileJob &) const = default;
};

struct SchedulerBudget
{
    int maxIntervalJobsPerFrame{256};
    int maxRasterJobsPerFrame{64};
    int maxContourJobsPerFrame{64};
    int maxUploadJobsPerFrame{64};
    std::chrono::microseconds maxMainThreadApplyTime{2000};
};

class TileScheduler
{
public:
    [[nodiscard]] std::vector<TileJob> buildJobs(const ViewportRequest &request) const;
    [[nodiscard]] std::vector<TileJob> buildJobs(const ViewportRequest &request,
                                                 const TileCache &tileCache,
                                                 const SchedulerBudget &budget) const;

private:
    static void appendNextJobForStage(std::vector<TileJob> &jobs,
                                      const ViewportRequest &request,
                                      const TileKey &key,
                                      TileStage stage,
                                      int priority);
    [[nodiscard]] static int priorityFor(const ViewportRequest &request, const TileKey &key);
};
}

#endif // TILESCHEDULER_H
