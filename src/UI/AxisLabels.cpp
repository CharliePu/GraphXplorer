//
// Created by charl on 6/8/2024.
//

#include "AxisLabels.h"

#include <cmath>
#include <format>
#include <ft2build.h>
#include <glad/glad.h>
#include <staplegl/staplegl.hpp>

#include FT_FREETYPE_H

#include "../Render/Mesh.h"
#include "../Core/Window.h"
#include "../Render/TextMeshesGenerator.h"
#include "../Util/PerformanceProfiler.h"

AxisLabels::AxisLabels(const std::shared_ptr<Window> &window): window(window)
{
}

int AxisLabels::getDepth() const
{
    return 4;
}

void AxisLabels::updateLabels(const Interval newXRange, const Interval newYRange)
{
    GRAPHX_PROFILE_SCOPE("axisLabels.updateLabels");
    xRange = newXRange;
    yRange = newYRange;
    const auto windowWidth = window->getWidth();
    const auto windowHeight = window->getHeight();
    if (windowWidth <= 0 || windowHeight <= 0)
    {
        return;
    }

    auto toNDC = [](double value, Interval range) -> double {
        return ((value - range.lower) / range.size() - 0.5) * 2;
    };

    auto &textGenerator{TextMeshesGenerator::getInstance()};

    const auto pickNiceStep = [](const double span, const int desiredTickCount) -> double
    {
        if (span <= 0.0 || desiredTickCount <= 0)
        {
            return 1.0;
        }

        const auto roughStep = span / static_cast<double>(desiredTickCount);
        const auto power = std::pow(10.0, std::floor(std::log10(roughStep)));
        const auto normalized = roughStep / power;

        auto niceNormalized = 10.0;
        if (normalized <= 1.0)
        {
            niceNormalized = 1.0;
        }
        else if (normalized <= 2.0)
        {
            niceNormalized = 2.0;
        }
        else if (normalized <= 5.0)
        {
            niceNormalized = 5.0;
        }

        return niceNormalized * power;
    };

    const auto firstTickAtOrAbove = [](const double lowerBound, const double step) -> double
    {
        constexpr auto epsilon = 1e-9;
        return std::ceil((lowerBound - epsilon) / step) * step;
    };

    constexpr auto desiredTickCount = 8;
    const auto xMajorGrid = pickNiceStep(newXRange.size() / window->getAspectRatio(), desiredTickCount);
    const auto yMajorGrid = pickNiceStep(newYRange.size(), desiredTickCount);

    const auto startX = firstTickAtOrAbove(newXRange.lower, xMajorGrid);
    const auto startY = firstTickAtOrAbove(newYRange.lower, yMajorGrid);

    const auto xAxisPos = std::min(0.95, std::max(-0.95, toNDC(0, newYRange)));
    const auto yAxisPos = std::min(0.9, std::max(-0.9, toNDC(0, newXRange)));

    std::vector<Mesh> labelMeshes;

    // X-axis labels
    constexpr auto loopEpsilon = 1e-9;
    {
        GRAPHX_PROFILE_SCOPE("axisLabels.updateLabels.xAxis");
        for (auto x = startX; x <= newXRange.upper + loopEpsilon; x += xMajorGrid)
        {
            const auto roundX = std::round(x * 1e8) / 1e8;

            if (roundX != 0)
            {
                const auto text = std::format("{:.8g}", roundX);
                const auto meshes = textGenerator.generateTextMesh(text, toNDC(x, newXRange), xAxisPos - 0.06, 0.001,
                                                                   TextAlign::CENTER,
                                                                   static_cast<double>(windowWidth) /
                                                                   static_cast<double>(windowHeight));
                labelMeshes.insert(labelMeshes.end(), meshes.begin(), meshes.end());
            }
        }
    }

    // Y-axis labels
    {
        GRAPHX_PROFILE_SCOPE("axisLabels.updateLabels.yAxis");
        for (auto y = startY; y <= newYRange.upper + loopEpsilon; y += yMajorGrid)
        {
            const auto roundY = std::round(y * 1e8) / 1e8;
            if (roundY != 0)
            {
                const auto text = std::format("{:.8g}", roundY);
                const auto meshes = textGenerator.generateTextMesh(
                    text, yAxisPos, toNDC(y, newYRange), 0.001, TextAlign::CENTER,
                    static_cast<double>(windowWidth) / static_cast<double>(windowHeight));
                labelMeshes.insert(labelMeshes.end(), meshes.begin(), meshes.end());
            }
        }
    }

    {
        GRAPHX_PROFILE_SCOPE("axisLabels.updateLabels.publish");
        updateLabelsCallback(labelMeshes);
    }
}

void AxisLabels::setUpdateLabelsCallback(const std::function<void(const std::vector<Mesh> &)> &callback)
{
    updateLabelsCallback = callback;
}

void AxisLabels::onWindowSizeChanged(int width, int height)
{
    GRAPHX_PROFILE_SCOPE("axisLabels.onWindowSizeChanged");
    (void)width;
    (void)height;
    updateLabels(xRange, yRange);
}
