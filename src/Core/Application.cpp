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
#include "../Math/GraphRasterizer.h"
#include "../Util/ThreadPool.h"

Application::Application(const int width, const int height, const std::string &name) :
    name{name},
    window{std::make_shared<Window>(width, height, name)},
    renderer{std::make_shared<Renderer>(reinterpret_cast<GLADloadproc>(glfw::getProcAddress))},
    input{std::make_shared<Input>(window)},
    threadPool{std::make_shared<ThreadPool>()},
    computeEngine{std::make_shared<ComputeEngine>(window, threadPool)},
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

    window->onWindowSizeChanged(width, height);
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

void Application::onCursorDrag(double x, double y)
{
    window->onCursorDrag(x, y);
    renderer->onCursorDrag(x, y);
    sceneManager->onCursorDrag(x, y);
}

void Application::onTextEntered(unsigned int codepoint)
{
    window->onTextEntered(codepoint);
    renderer->onTextEntered(codepoint);
    sceneManager->onTextEntered(codepoint);
}

