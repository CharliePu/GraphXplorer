//
// Created by Codex on 2/27/2026.
//

#include "CpuChunkRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "../Formula/Formula.h"
#include "../Formula/Evaluator.h"

bool CpuChunkRenderer::rasterizeMixedChunkTexture(const std::shared_ptr<Formula> &formula,
                                                  const Interval &xRange,
                                                  const Interval &yRange,
                                                  const int textureSize,
                                                  std::vector<int> &outputPixels)
{
    if (!formula || textureSize <= 0)
    {
        outputPixels.clear();
        return false;
    }

    outputPixels.assign(static_cast<size_t>(textureSize) * static_cast<size_t>(textureSize), 0);

    const auto deltaX = xRange.size() / static_cast<double>(textureSize);
    const auto deltaY = yRange.size() / static_cast<double>(textureSize);

    for (int y = 0; y < textureSize; ++y)
    {
        const auto sampleY = yRange.lower + (static_cast<double>(y) + 0.5) * deltaY;
        for (int x = 0; x < textureSize; ++x)
        {
            const auto sampleX = xRange.lower + (static_cast<double>(x) + 0.5) * deltaX;
            const auto idx = y * textureSize + x;

            try
            {
                const auto value = formula->evaluate({{"x", sampleX}, {"y", sampleY}});
                outputPixels[static_cast<size_t>(idx)] = value > 0.0 ? 1 : 0;
            }
            catch (...)
            {
                outputPixels[static_cast<size_t>(idx)] = 0;
            }
        }
    }

    return true;
}

bool CpuChunkRenderer::rasterizeChunkContourSegments(const RPN &residualRpn,
                                                     const Interval &xRange,
                                                     const Interval &yRange,
                                                     const int cellsPerAxis,
                                                     std::vector<RasterContourSegment> &outputSegments)
{
    outputSegments.clear();

    if (cellsPerAxis < 1)
    {
        return false;
    }

    const auto sampleSize = cellsPerAxis + 1;
    std::vector<float> sampledValues(
        static_cast<size_t>(sampleSize) * static_cast<size_t>(sampleSize),
        0.0f);

    const auto dx = xRange.size() / static_cast<double>(sampleSize - 1);
    const auto dy = yRange.size() / static_cast<double>(sampleSize - 1);

    Evaluator<double> evaluator;
    for (int y = 0; y < sampleSize; ++y)
    {
        const auto sampleY = yRange.lower + static_cast<double>(y) * dy;
        for (int x = 0; x < sampleSize; ++x)
        {
            const auto sampleX = xRange.lower + static_cast<double>(x) * dx;
            const auto idx = static_cast<size_t>(y) * static_cast<size_t>(sampleSize)
                + static_cast<size_t>(x);

            try
            {
                const auto value = evaluator.evaluateRPN(residualRpn, {{"x", sampleX}, {"y", sampleY}});
                sampledValues[idx] = static_cast<float>(value);
            }
            catch (...)
            {
                sampledValues[idx] = std::numeric_limits<float>::quiet_NaN();
            }
        }
    }

    const auto interpolate = [](const double v0,
                                const double v1,
                                const double p0x,
                                const double p0y,
                                const double p1x,
                                const double p1y) -> std::pair<double, double>
    {
        constexpr auto epsilon = 1e-12;
        auto t = 0.5;
        const auto denom = v0 - v1;
        if (std::isfinite(denom) && std::abs(denom) > epsilon)
        {
            t = v0 / denom;
        }
        t = std::clamp(t, 0.0, 1.0);
        return {p0x + (p1x - p0x) * t, p0y + (p1y - p0y) * t};
    };

    const auto appendSegment = [&outputSegments](const std::pair<double, double> &p0,
                                                 const std::pair<double, double> &p1)
    {
        constexpr auto minLength2 = 1e-20;
        const auto dx = p0.first - p1.first;
        const auto dy = p0.second - p1.second;
        if (dx * dx + dy * dy <= minLength2)
        {
            return;
        }

        outputSegments.push_back({p0.first, p0.second, p1.first, p1.second});
    };

    const auto sampleAt = [&sampledValues, sampleSize](const int x, const int y) -> double
    {
        const auto idx = static_cast<size_t>(y) * static_cast<size_t>(sampleSize) + static_cast<size_t>(x);
        return static_cast<double>(sampledValues[idx]);
    };

    for (int cellY = 0; cellY < cellsPerAxis; ++cellY)
    {
        for (int cellX = 0; cellX < cellsPerAxis; ++cellX)
        {
            const auto values = std::array{
                sampleAt(cellX, cellY), // bottom-left
                sampleAt(cellX + 1, cellY), // bottom-right
                sampleAt(cellX + 1, cellY + 1), // top-right
                sampleAt(cellX, cellY + 1) // top-left
            };

            if (!std::isfinite(values[0]) || !std::isfinite(values[1]) || !std::isfinite(values[2]) || !std::isfinite(
                    values[3]))
            {
                continue;
            }

            const auto mask = (values[0] > 0.0 ? 1 : 0)
                              | (values[1] > 0.0 ? 2 : 0)
                              | (values[2] > 0.0 ? 4 : 0)
                              | (values[3] > 0.0 ? 8 : 0);

            if (mask == 0 || mask == 15)
            {
                continue;
            }

            const auto cellMinX = xRange.lower + static_cast<double>(cellX) * dx;
            const auto cellMaxX = cellMinX + dx;
            const auto cellMinY = yRange.lower + static_cast<double>(cellY) * dy;
            const auto cellMaxY = cellMinY + dy;

            const auto edgePoint = [&](const int edge) -> std::pair<double, double>
            {
                switch (edge)
                {
                case 0:
                    return interpolate(values[0], values[1], cellMinX, cellMinY, cellMaxX, cellMinY);
                case 1:
                    return interpolate(values[1], values[2], cellMaxX, cellMinY, cellMaxX, cellMaxY);
                case 2:
                    return interpolate(values[2], values[3], cellMaxX, cellMaxY, cellMinX, cellMaxY);
                case 3:
                default:
                    return interpolate(values[3], values[0], cellMinX, cellMaxY, cellMinX, cellMinY);
                }
            };

            switch (mask)
            {
            case 1:
            case 14:
                appendSegment(edgePoint(3), edgePoint(0));
                break;
            case 2:
            case 13:
                appendSegment(edgePoint(0), edgePoint(1));
                break;
            case 3:
            case 12:
                appendSegment(edgePoint(3), edgePoint(1));
                break;
            case 4:
            case 11:
                appendSegment(edgePoint(1), edgePoint(2));
                break;
            case 5:
                appendSegment(edgePoint(3), edgePoint(2));
                appendSegment(edgePoint(0), edgePoint(1));
                break;
            case 6:
            case 9:
                appendSegment(edgePoint(0), edgePoint(2));
                break;
            case 7:
            case 8:
                appendSegment(edgePoint(3), edgePoint(2));
                break;
            case 10:
                appendSegment(edgePoint(0), edgePoint(1));
                appendSegment(edgePoint(2), edgePoint(3));
                break;
            default:
                break;
            }
        }
    }

    return true;
}
