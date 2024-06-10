//
// Created by charl on 6/3/2024.
//

#include "MainScene.h"

#include "../Math/ComputeEngine.h"
#include "../Render/Renderer.h"
#include "../UI/Plot.h"
#include "../UI/ConsoleInput.h"
#include "../UI/Grid.h"
#include "../UI/AxisLabels.h"


MainScene::MainScene(const std::shared_ptr<ComputeEngine> &engine, const std::shared_ptr<Renderer> &renderer, const std::shared_ptr<Window> &window):
    plot(std::make_shared<Plot>(engine, window)),
    grid(std::make_shared<Grid>(window)),
    axisLabels(std::make_shared<AxisLabels>(window)),
    cmd(std::make_shared<ConsoleInput>())
{
    cmd->setInputCompleteCallback([this, engine](const std::string &input) {
        plot->requestNewPlot(input);
    });

    plot->setPlotCompleteCallback([this, renderer](const std::vector<Mesh> &meshes) {
        grid->updatePosition(plot->getXRanges(), plot->getYRanges());
        axisLabels->updateLabels(plot->getXRanges(), plot->getYRanges());
        renderer->updateMeshes(plot, meshes);
    });

    grid->setUpdatePositionCallback([this, renderer](const std::vector<Mesh> &meshes)
    {
       renderer->updateMeshes(grid, meshes);
    });

    axisLabels->setUpdateLabelsCallback([this, renderer](const std::vector<Mesh> &meshes)
    {
        renderer->updateMeshes(axisLabels, meshes);
    });
}

void MainScene::onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods)
{
    plot->onKeyPressed(key, scancode, action, mods);
    grid->onKeyPressed(key, scancode, action, mods);
    axisLabels->onKeyPressed(key, scancode, action, mods);
    cmd->onKeyPressed(key, scancode, action, mods);
}

void MainScene::onCursorDrag(const double x, const double y)
{
    plot->onCursorDrag(x, y);
    grid->onCursorDrag(x, y);
    axisLabels->onCursorDrag(x, y);
    cmd->onCursorDrag(x, y);
}

void MainScene::onWindowSizeChanged(const int width, const int height)
{
    plot->onWindowSizeChanged(width, height);
    grid->onWindowSizeChanged(width, height);
    axisLabels->onWindowSizeChanged(width, height);
    cmd->onWindowSizeChanged(width, height);
}
