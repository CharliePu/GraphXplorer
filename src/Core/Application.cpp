//
// Created by charl on 6/2/2024.
//

#include "Application.h"

#include "Window.h"
#include "../Render/Renderer.h"
#include "../Scene/SceneManager.h"

#include <glad/glad.h>

#include <utility>

#include "../Math/ComputeEngine.h"

Application::Application(const int width, const int height, const std::string &name) :
    name{name},
    window{std::make_shared<Window>(width, height, name)},
    renderer{std::make_shared<Renderer>(reinterpret_cast<GLADloadproc>(glfw::getProcAddress))},
    input{std::make_shared<Input>(window)},
    computeEngine{std::make_shared<ComputeEngine>(window)},
    sceneManager{std::make_shared<SceneManager>(computeEngine, renderer, window)}
{
}

void Application::run()
{
    input->setInputHandlers({shared_from_this()});

    while (!window->shouldClose())
    {
        computeEngine->pollAsyncStates();

        renderer->clear();
        renderer->draw();

        glfw::waitEvents();
        window->swapBuffers();
    }
}

void Application::onWindowSizeChanged(const int width, const int height)
{
    // Swap buffers to allow smooth resizing
    window->swapBuffers();

    renderer->onWindowSizeChanged(width, height);
    sceneManager->onWindowSizeChanged(width, height);
}

void Application::onKeyPressed(const glfw::KeyCode key, const int scancode, const glfw::KeyState action,
                               const glfw::ModifierKeyBit mods)
{
    window->onKeyPressed(key, scancode, action, mods);
    renderer->onKeyPressed(key, scancode, action, mods);
    sceneManager->onKeyPressed(key, scancode, action, mods);
}

