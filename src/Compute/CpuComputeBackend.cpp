#include "ComputeBackend.h"

#include <algorithm>

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
        if (interval.allTrue())
        {
            classification = TileClassification::UniformTrue;
        }
        else if (interval.allFalse())
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

    const auto pixelsPerAxis = static_cast<int>(batch.pixelsPerAxis);
    const auto xSlot = batch.formula->variableSlot("x");
    const auto ySlot = batch.formula->variableSlot("y");
    for (size_t i = 0; i < batch.keys.size(); ++i)
    {
        auto &output = out[i];
        output.key = batch.keys[i];
        output.width = batch.pixelsPerAxis;
        output.height = batch.pixelsPerAxis;
        output.pixels.assign(static_cast<size_t>(pixelsPerAxis) * static_cast<size_t>(pixelsPerAxis), 0);

        const auto dx = (batch.xMax[i] - batch.xMin[i]) / static_cast<double>(pixelsPerAxis);
        const auto dy = (batch.yMax[i] - batch.yMin[i]) / static_cast<double>(pixelsPerAxis);
        std::vector<double> variables(batch.formula->variableNames.size(), 0.0);

        for (auto y = 0; y < pixelsPerAxis; ++y)
        {
            if (batch.cancelled && batch.cancelled())
            {
                return {false, i, "Cancelled"};
            }

            const auto sampleY = batch.yMin[i] + (static_cast<double>(y) + 0.5) * dy;
            if (ySlot)
            {
                variables[*ySlot] = sampleY;
            }
            for (auto x = 0; x < pixelsPerAxis; ++x)
            {
                const auto sampleX = batch.xMin[i] + (static_cast<double>(x) + 0.5) * dx;
                if (xSlot)
                {
                    variables[*xSlot] = sampleX;
                }
                const auto idx = static_cast<size_t>(y) * static_cast<size_t>(pixelsPerAxis) + static_cast<size_t>(x);
                try
                {
                    const auto value = batch.formula->evaluateDouble(variables);
                    output.pixels[idx] = value > 0.0 ? uint8_t{255} : uint8_t{0};
                }
                catch (...)
                {
                    output.pixels[idx] = 0;
                }
            }
        }
    }

    return {true, batch.keys.size(), {}};
}
}
