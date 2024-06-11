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

void AxisLabels::updateLabels(Interval<double> xRange, Interval<double> yRange)
{
    auto toNDC = [](double value, Interval<double> range) -> double {
        return ((value - range.lower) / range.size() - 0.5) * 2;
    };

    auto &textGenerator{TextMeshesGenerator::getInstance()};

    const auto xMajorGrid{std::pow(10.0, std::floor(std::log10(yRange.size())))};
    const auto yMajorGrid{std::pow(10.0, std::floor(std::log10(yRange.size())))};

    const auto startX = xRange.lower + std::fmod((xMajorGrid - std::fmod(xRange.lower, xMajorGrid)), xMajorGrid);
    const auto startY = yRange.lower + std::fmod((yMajorGrid - std::fmod(yRange.lower, yMajorGrid)), yMajorGrid);

    const auto xAxisPos = std::min(0.95, std::max(-0.95, toNDC(0, yRange)));
    const auto yAxisPos = std::min(0.9, std::max(-0.9, toNDC(0, xRange)));

    std::vector<Mesh> labelMeshes;

    // X-axis labels
    for (auto x = startX; x <= xRange.upper;)
    {
        const auto roundX = std::round(x * 1e8) / 1e8;

        if (roundX != 0)
        {
            const auto text = std::format("{:.8g}", roundX);
            const auto meshes = textGenerator.generateTextMesh(text, toNDC(x, xRange), xAxisPos - 0.06, 0.001,
                                                               TextAlign::CENTER,
                                                               static_cast<double>(window->getWidth()) / window->
                                                               getHeight());
            labelMeshes.insert(labelMeshes.end(), meshes.begin(), meshes.end());
        }
        x += xMajorGrid;
    }

    // Y-axis labels
    for (auto y = startY; y <= yRange.upper;)
    {
        const auto roundY = std::round(y * 1e8) / 1e8;
        if (roundY != 0)
        {
            const auto text = std::format("{:.8g}", roundY);
            const auto meshes = textGenerator.generateTextMesh(text, yAxisPos, toNDC(y, yRange), 0.001, TextAlign::CENTER,
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
