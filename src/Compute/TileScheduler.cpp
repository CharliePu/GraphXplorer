#include "TileScheduler.h"

#include <algorithm>
#include <cmath>

#include "../Tile/TileMath.h"
#include "../Tile/TileCache.h"

namespace gx
{
int TileScheduler::priorityFor(const ViewportRequest &request, const TileKey &key)
{
    const auto bounds = tileBounds(key);
    const auto centerX = (bounds.xMin + bounds.xMax) * 0.5;
    const auto centerY = (bounds.yMin + bounds.yMax) * 0.5;
    const auto requestCenterX = request.xRange.mid();
    const auto requestCenterY = request.yRange.mid();
    const auto dx = centerX - requestCenterX;
    const auto dy = centerY - requestCenterY;
    const auto distance = std::sqrt(dx * dx + dy * dy);
    return static_cast<int>(distance * 1000.0);
}

std::vector<TileJob> TileScheduler::buildJobs(const ViewportRequest &request) const
{
    std::vector<TileJob> jobs;
    if (!request.valid())
    {
        return jobs;
    }

    const auto level = targetTileLevel(
        request.xRange,
        request.yRange,
        request.framebufferWidth,
        request.framebufferHeight);
    const auto [minX, maxX] = tileIndexBounds(request.xRange, level);
    const auto [minY, maxY] = tileIndexBounds(request.yRange, level);

    for (auto y = minY; y <= maxY; ++y)
    {
        for (auto x = minX; x <= maxX; ++x)
        {
            const TileKey key{x, y, level};
            const auto basePriority = priorityFor(request, key);
            jobs.push_back({
                JobKind::ClassifyInterval,
                WorkClass::VisibleNow,
                key,
                request.header.requestId,
                request.header.generation,
                basePriority,
                {}
            });
            jobs.push_back({
                JobKind::RasterizeRegion,
                WorkClass::VisibleRefinement,
                key,
                request.header.requestId,
                request.header.generation,
                basePriority + 100000,
                {.interval = true}
            });
            if (request.formula.traits.supportsContour)
            {
                jobs.push_back({
                    JobKind::ExtractContour,
                    WorkClass::VisibleRefinement,
                    key,
                    request.header.requestId,
                    request.header.generation,
                    basePriority + 150000,
                    {.interval = true}
                });
            }
            jobs.push_back({
                JobKind::StageUpload,
                WorkClass::Upload,
                key,
                request.header.requestId,
                request.header.generation,
                basePriority + 200000,
                {.region = true}
            });
            jobs.push_back({
                JobKind::PublishDelta,
                WorkClass::VisibleNow,
                key,
                request.header.requestId,
                request.header.generation,
                basePriority + 250000,
                {.upload = true}
            });
        }
    }

    std::ranges::sort(jobs, [](const TileJob &lhs, const TileJob &rhs)
    {
        if (lhs.priority != rhs.priority)
        {
            return lhs.priority < rhs.priority;
        }
        return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
    });

    return jobs;
}

void TileScheduler::appendNextJobForStage(std::vector<TileJob> &jobs,
                                          const ViewportRequest &request,
                                          const TileKey &key,
                                          const TileStage stage,
                                          const int priority)
{
    switch (stage)
    {
    case TileStage::Unknown:
    case TileStage::Evicted:
        jobs.push_back({
            JobKind::ClassifyInterval,
            WorkClass::VisibleNow,
            key,
            request.header.requestId,
            request.header.generation,
            priority,
            {}
        });
        break;
    case TileStage::MixedNeedsRegion:
        jobs.push_back({
            JobKind::RasterizeRegion,
            WorkClass::VisibleRefinement,
            key,
            request.header.requestId,
            request.header.generation,
            priority,
            {.interval = true}
        });
        break;
    case TileStage::RegionReady:
    case TileStage::ContourReady:
    case TileStage::UniformTrue:
    case TileStage::UniformFalse:
        jobs.push_back({
            JobKind::StageUpload,
            WorkClass::Upload,
            key,
            request.header.requestId,
            request.header.generation,
            priority,
            {}
        });
        break;
    case TileStage::GpuResident:
        jobs.push_back({
            JobKind::PublishDelta,
            WorkClass::VisibleNow,
            key,
            request.header.requestId,
            request.header.generation,
            priority,
            {.upload = true}
        });
        break;
    default:
        break;
    }
}

std::vector<TileJob> TileScheduler::buildJobs(const ViewportRequest &request,
                                              const TileCache &tileCache,
                                              const SchedulerBudget &budget) const
{
    std::vector<TileJob> jobs;
    if (!request.valid())
    {
        return jobs;
    }

    auto intervalBudget = budget.maxIntervalJobsPerFrame;
    auto rasterBudget = budget.maxRasterJobsPerFrame;
    auto uploadBudget = budget.maxUploadJobsPerFrame;

    const auto level = targetTileLevel(
        request.xRange,
        request.yRange,
        request.framebufferWidth,
        request.framebufferHeight);
    const auto [minX, maxX] = tileIndexBounds(request.xRange, level);
    const auto [minY, maxY] = tileIndexBounds(request.yRange, level);

    for (auto y = minY; y <= maxY; ++y)
    {
        for (auto x = minX; x <= maxX; ++x)
        {
            const TileKey key{x, y, level};
            const auto *record = tileCache.find(key, request.formula.semanticsHash);
            const auto stage = record ? record->stage : TileStage::Unknown;
            const auto before = jobs.size();
            appendNextJobForStage(jobs, request, key, stage, priorityFor(request, key));
            if (jobs.size() == before)
            {
                continue;
            }

            const auto kind = jobs.back().kind;
            if (kind == JobKind::ClassifyInterval && --intervalBudget < 0)
            {
                jobs.pop_back();
            }
            else if (kind == JobKind::RasterizeRegion && --rasterBudget < 0)
            {
                jobs.pop_back();
            }
            else if ((kind == JobKind::StageUpload || kind == JobKind::PublishDelta) && --uploadBudget < 0)
            {
                jobs.pop_back();
            }
        }
    }

    std::ranges::sort(jobs, [](const TileJob &lhs, const TileJob &rhs)
    {
        if (lhs.priority != rhs.priority)
        {
            return lhs.priority < rhs.priority;
        }
        return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
    });

    return jobs;
}
}
