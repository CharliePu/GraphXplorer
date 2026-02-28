//
// Created by charl on 6/3/2024.
//

#include "MainScene.h"

#include "../Math/GraphProcessor.h"
#include "../Render/Renderer.h"
#include "../UI/Plot.h"
#include "../UI/ConsoleInput.h"
#include "../UI/Grid.h"
#include "../UI/AxisLabels.h"
#include "../UI/InputBox.h"

MainScene::MainScene(const std::shared_ptr<ComputeEngine> &engine,
                     const std::shared_ptr<Renderer> &renderer,
                     const std::shared_ptr<Window> &window): plot(std::make_shared<Plot>(
                                                                 engine, window)),
                                                             grid(std::make_shared<Grid>(window)),
                                                             axisLabels(std::make_shared<AxisLabels>(window)),
                                                             cmd(std::make_shared<ConsoleInput>()),
                                                             inputBox(std::make_shared<InputBox>(window))
{
    cmd->setInputCompleteCallback([this](const std::string &input) {
        plot->requestNewPlot(input);
    });

    inputBox->setUpdateStateCallback([this, renderer](const std::vector<Mesh> &meshes) {
        renderer->updateMeshes(inputBox, meshes);
    });

    inputBox->setInputCompleteCallback([this](const std::string &input) {
        plot->requestNewPlot(input);
    });

    plot->setPlotRangeChangedCallback([this, renderer](const Interval &xRange, const Interval &yRange) {
        grid->updatePosition(xRange, yRange);
        axisLabels->updateLabels(xRange, yRange);
    });

    plot->setPlotCompleteCallback([this, renderer](const std::vector<Mesh> &meshes) {
        renderer->updateMeshes(plot, meshes);
    });

    grid->setUpdatePositionCallback([this, renderer](const std::vector<Mesh> &meshes) {
        renderer->updateMeshes(grid, meshes);
    });

    axisLabels->setUpdateLabelsCallback([this, renderer](const std::vector<Mesh> &meshes) {
        renderer->updateMeshes(axisLabels, meshes);
    });

    plot->requestNewPlot("x<=y");
    grid->updatePosition(plot->getXRanges(), plot->getYRanges());
    axisLabels->updateLabels(plot->getXRanges(), plot->getYRanges());
}

void MainScene::onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods)
{
    if (interactionState == InteractionState::FormulaInput)
    {
        inputBox->onKeyPressed(key, scancode, action, mods);
        if (!inputBox->isCapturingInput())
        {
            transitionToState(InteractionState::Navigation);
        }
        return;
    }

    if (key == glfw::KeyCode::I && action == glfw::KeyState::Release)
    {
        transitionToState(InteractionState::FormulaInput);
        return;
    }

    plot->onKeyPressed(key, scancode, action, mods);
    grid->onKeyPressed(key, scancode, action, mods);
    axisLabels->onKeyPressed(key, scancode, action, mods);
    cmd->onKeyPressed(key, scancode, action, mods);
}

void MainScene::onCursorDrag(const double x, const double y)
{
    if (interactionState == InteractionState::FormulaInput)
    {
        return;
    }

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
    inputBox->onWindowSizeChanged(width, height);
}

void MainScene::onTextEntered(unsigned int codepoint)
{
    if (interactionState == InteractionState::FormulaInput)
    {
        inputBox->onTextEntered(codepoint);
        return;
    }

    plot->onTextEntered(codepoint);
    grid->onTextEntered(codepoint);
    axisLabels->onTextEntered(codepoint);
    cmd->onTextEntered(codepoint);
}

void MainScene::onMouseScrolled(double offset)
{
    if (interactionState == InteractionState::FormulaInput)
    {
        return;
    }

    plot->onMouseScrolled(offset);
    grid->onMouseScrolled(offset);
    axisLabels->onMouseScrolled(offset);
    cmd->onMouseScrolled(offset);
}

void MainScene::transitionToState(const InteractionState nextState)
{
    if (interactionState == nextState)
    {
        return;
    }

    interactionState = nextState;

    if (interactionState == InteractionState::FormulaInput)
    {
        inputBox->beginInput();
    }
}
