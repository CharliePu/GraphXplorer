//
// Created by charl on 6/3/2024.
//

#include "MainScene.h"

#include "../Math/ComputeEngine.h"
#include "../Render/Renderer.h"
#include "../UI/Plot.h"
#include "../UI/ConsoleInput.h"


MainScene::MainScene(const std::shared_ptr<ComputeEngine> &engine, const std::shared_ptr<Renderer> &renderer, const std::shared_ptr<Window> &window):
    plot(std::make_shared<Plot>(engine, window)),
    cmd(std::make_shared<ConsoleInput>())
{
    cmd->setInputCompleteCallback([this, engine](const std::string &input) {
        plot->requestNewPlot(input);
    });

    plot->setPlotCompleteCallback([this, renderer](const std::vector<Mesh> &meshes) {
        renderer->updateMeshes(plot, meshes);
    });
}

void MainScene::onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods)
{
    plot->onKeyPressed(key, scancode, action, mods);
    cmd->onKeyPressed(key, scancode, action, mods);
}
