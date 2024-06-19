//
// Created by charl on 6/8/2024.
//

#include "AxisLabels.h"

#include <format>
#include <ft2build.h>
#include <glad/glad.h>
#include <staplegl/staplegl.hpp>

#include FT_FREETYPE_H

#include "../Render/Mesh.h"
#include "../Core/Window.h"
#include "../Render/TextMeshesGenerator.h"

AxisLabels::AxisLabels(const std::shared_ptr<Window> &window): window(window)
{
}

int AxisLabels::getDepth() const
{
    return 4;
}

void AxisLabels::updateLabels(const Interval<double> newXRange, const Interval<double> newYRange)
{
    xRange = newXRange;
    yRange = newYRange;

    auto toNDC = [](double value, Interval<double> range) -> double {
        return ((value - range.lower) / range.size() - 0.5) * 2;
    };

    auto &textGenerator{TextMeshesGenerator::getInstance()};

    const auto xLog = std::floor(std::log10(newXRange.size() / window->getAspectRatio()));
    const auto yLog = std::floor(std::log10(newYRange.size()));
    const auto xMajorGrid = std::pow(10.0, xLog);
    const auto yMajorGrid = std::pow(10.0, yLog);

    const auto startX = newXRange.lower + std::fmod((xMajorGrid - std::fmod(newXRange.lower, xMajorGrid)), xMajorGrid);
    const auto startY = newYRange.lower + std::fmod((yMajorGrid - std::fmod(newYRange.lower, yMajorGrid)), yMajorGrid);

    const auto xAxisPos = std::min(0.95, std::max(-0.95, toNDC(0, newYRange)));
    const auto yAxisPos = std::min(0.9, std::max(-0.9, toNDC(0, newXRange)));

    std::vector<Mesh> labelMeshes;

    // X-axis labels
    for (auto x = startX; x <= newXRange.upper;)
    {
        const auto roundX = std::round(x * 1e8) / 1e8;

        if (roundX != 0)
        {
            const auto text = std::format("{:.8g}", roundX);
            const auto meshes = textGenerator.generateTextMesh(text, toNDC(x, newXRange), xAxisPos - 0.06, 0.001,
                                                               TextAlign::CENTER,
                                                               static_cast<double>(window->getWidth()) / window->
                                                               getHeight());
            labelMeshes.insert(labelMeshes.end(), meshes.begin(), meshes.end());
        }
        x += xMajorGrid;
    }

    // Y-axis labels
    for (auto y = startY; y <= newYRange.upper;)
    {
        const auto roundY = std::round(y * 1e8) / 1e8;
        if (roundY != 0)
        {
            const auto text = std::format("{:.8g}", roundY);
            const auto meshes = textGenerator.generateTextMesh(text, yAxisPos, toNDC(y, newYRange), 0.001, TextAlign::CENTER,
                                                               static_cast<double>(window->getWidth()) / window->
                                                               getHeight());
            labelMeshes.insert(labelMeshes.end(), meshes.begin(), meshes.end());
        }
        y += yMajorGrid;
    }

    updateLabelsCallback(labelMeshes);
}

void AxisLabels::setUpdateLabelsCallback(const std::function<void(const std::vector<Mesh> &)> &callback)
{
    updateLabelsCallback = callback;
}

void AxisLabels::onWindowSizeChanged(int width, int height)
{
    updateLabels(xRange, yRange);
}
