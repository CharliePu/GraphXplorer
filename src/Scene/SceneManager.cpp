//
// Created by charl on 6/3/2024.
//

#include "SceneManager.h"

#include "MainScene.h"
#include "../Math/GraphRasterizer.h"

SceneManager::SceneManager(const std::shared_ptr<ComputeEngine> &engine,
                           const std::shared_ptr<Renderer> &renderer,
                           const std::shared_ptr<Window> &window)
    : mainScene{std::make_shared<MainScene>(engine, renderer, window)},
      currentScene{mainScene}
{
}

void SceneManager::onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods)
{
    currentScene->onKeyPressed(key, scancode, action, mods);
}

void SceneManager::onCursorDrag(double x, double y)
{
    currentScene->onCursorDrag(x, y);
}

void SceneManager::onWindowSizeChanged(int width, int height)
{
    currentScene->onWindowSizeChanged(width, height);
}

void SceneManager::onTextEntered(unsigned int codepoint)
{
    currentScene->onTextEntered(codepoint);
}
