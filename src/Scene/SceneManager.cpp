//
// Created by charl on 6/3/2024.
//

#include "SceneManager.h"

#include "MainScene.h"

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
