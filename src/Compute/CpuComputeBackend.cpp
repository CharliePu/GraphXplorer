#include "ComputeBackend.h"

#include <algorithm>
#include <cstdint>

namespace gx
{
namespace
{
constexpr auto PixelFalse = uint8_t{0};
constexpr auto PixelUnknown = uint8_t{127};
constexpr auto PixelTrue = uint8_t{255};
constexpr auto RasterSupersampleAxis = 8;
constexpr auto RasterSupersampleCount = RasterSupersampleAxis * RasterSupersampleAxis;

[[nodiscard]] uint8_t supersampledAnyHit(const CompiledFormula &formula,
                                         std::vector<double> &variables,
                                         const std::optional<size_t> xSlot,
                                         const std::optional<size_t> ySlot,
                                         const double x0,
                                         const double y0,
                                         const double dx,
                                         const double dy)
{
    for (auto sample = 0; sample < RasterSupersampleCount; ++sample)
    {
        const auto sx = sample % RasterSupersampleAxis;
        const auto sy = sample / RasterSupersampleAxis;
        const auto sampleX = x0
            + ((static_cast<double>(sx) + 0.5) / RasterSupersampleAxis)
            * dx;
        const auto sampleY = y0
            + ((static_cast<double>(sy) + 0.5) / RasterSupersampleAxis)
            * dy;
        if (xSlot)
        {
            variables[*xSlot] = sampleX;
        }
        if (ySlot)
        {
            variables[*ySlot] = sampleY;
        }

        try
        {
            if (formula.evaluateDouble(variables) > 0.0)
            {
                return PixelTrue;
            }
        }
        catch (...)
        {
        }
    }
    return PixelFalse;
}

[[nodiscard]] uint8_t rasterizedPixelValue(const CompiledFormula &formula,
                                           std::vector<Interval> &intervalVariables,
                                           std::vector<double> &sampleVariables,
                                           const std::optional<size_t> xSlot,
                                           const std::optional<size_t> ySlot,
                                           const double x0,
                                           const double x1,
                                           const double y0,
                                           const double y1)
{
    const auto dx = x1 - x0;
    const auto dy = y1 - y0;
    if (xSlot)
    {
        intervalVariables[*xSlot] = Interval{x0, x1};
    }
    if (ySlot)
    {
        intervalVariables[*ySlot] = Interval{y0, y1};
    }

    try
    {
        const auto interval = formula.evaluateInterval(intervalVariables);
        if (interval.hasDiscontinuity() || interval.undefined())
        {
            return PixelUnknown;
        }
        if (interval.allTrue())
        {
            return PixelTrue;
        }
        if (interval.allFalse())
        {
            return PixelFalse;
        }
    }
    catch (...)
    {
        return PixelUnknown;
    }

    return supersampledAnyHit(formula, sampleVariables, xSlot, ySlot, x0, y0, dx, dy);
}
}

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
        std::vector<Interval> intervalVariables(batch.formula->variableNames.size(), Interval{0.0});
        std::vector<double> sampleVariables(batch.formula->variableNames.size(), 0.0);

        for (auto y = 0; y < pixelsPerAxis; ++y)
        {
            if (batch.cancelled && batch.cancelled())
            {
                return {false, i, "Cancelled"};
            }

            const auto y0 = batch.yMin[i] + static_cast<double>(y) * dy;
            const auto y1 = y0 + dy;
            for (auto x = 0; x < pixelsPerAxis; ++x)
            {
                const auto x0 = batch.xMin[i] + static_cast<double>(x) * dx;
                const auto x1 = x0 + dx;
                const auto idx = static_cast<size_t>(y) * static_cast<size_t>(pixelsPerAxis) + static_cast<size_t>(x);
                output.pixels[idx] = rasterizedPixelValue(
                    *batch.formula,
                    intervalVariables,
                    sampleVariables,
                    xSlot,
                    ySlot,
                    x0,
                    x1,
                    y0,
                    y1);
            }
        }
    }

    return {true, batch.keys.size(), {}};
}

}
