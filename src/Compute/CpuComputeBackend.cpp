#include "ComputeBackend.h"
#include "InequalityTileRefiner.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace gx
{
BackendCapabilities CpuComputeBackend::capabilities() const
{
    return {
        .supportsIntervalClassification = true,
        .supportsRegionRaster = true,
        .supportsContourExtraction = false,
        .supportsOpenCl = false
    };
}

BatchResult CpuComputeBackend::classifyIntervals(const IntervalBatchView &batch,
                                                 std::span<TileClassificationResult> out)
{
    if (!batch.formula || batch.keys.size() != out.size()
        || batch.xMin.size() != batch.keys.size()
        || batch.xMax.size() != batch.keys.size()
        || batch.yMin.size() != batch.keys.size()
        || batch.yMax.size() != batch.keys.size())
    {
        return {false, 0, "Invalid interval batch shape"};
    }

    for (size_t i = 0; i < batch.keys.size(); ++i)
    {
        if (batch.cancelled && batch.cancelled())
        {
            return {false, i, "Cancelled"};
        }

        std::vector<Interval> variables(batch.formula->variableNames.size(), Interval{0.0});
        if (const auto slot = batch.formula->variableSlot("x"))
        {
            variables[*slot] = Interval{batch.xMin[i], batch.xMax[i]};
        }
        if (const auto slot = batch.formula->variableSlot("y"))
        {
            variables[*slot] = Interval{batch.yMin[i], batch.yMax[i]};
        }

        const auto interval = batch.formula->evaluateInterval(variables);

        auto classification = TileClassification::Mixed;
        if (!interval.hasDiscontinuity() && !interval.undefined() && interval.allTrue())
        {
            classification = TileClassification::UniformTrue;
        }
        else if (!interval.hasDiscontinuity() && !interval.undefined() && interval.allFalse())
        {
            classification = TileClassification::UniformFalse;
        }

        out[i] = {batch.keys[i], classification, interval};
    }

    return {true, batch.keys.size(), {}};
}

BatchResult CpuComputeBackend::rasterizeRegions(const RasterBatchView &batch, std::span<RegionOutput> out)
{
    if (!batch.formula || batch.keys.size() != out.size()
        || batch.xMin.size() != batch.keys.size()
        || batch.xMax.size() != batch.keys.size()
        || batch.yMin.size() != batch.keys.size()
        || batch.yMax.size() != batch.keys.size()
        || batch.pixelsPerAxis == 0)
    {
        return {false, 0, "Invalid raster batch shape"};
    }

    for (size_t i = 0; i < batch.keys.size(); ++i)
    {
        if (batch.cancelled && batch.cancelled())
        {
            return {false, i, "Cancelled"};
        }

        auto refinement = refineInequalityTile(
            *batch.formula,
            batch.keys[i],
            InequalityTileRefinementOptions{
                .pixelsPerAxis = batch.pixelsPerAxis,
                .subpixelExtraDepth = DefaultRasterProofExtraDepth,
                .rootBounds = Rect{batch.xMin[i], batch.xMax[i], batch.yMin[i], batch.yMax[i]},
                .cancelled = batch.cancelled
            });
        if (!refinement.ok)
        {
            return {false, i, refinement.message};
        }
        out[i] = std::move(refinement.region);
    }

    return {true, batch.keys.size(), {}};
}

}
