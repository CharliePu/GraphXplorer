//
// Created by charl on 6/2/2024.
//

#include "Application.h"

#include <iostream>
#include <utility>

#include "Window.h"
#include "../Render/Renderer.h"
#include "../Scene/SceneManager.h"

#include <glad/glad.h>

#include "../Math/ComputeEngine.h"
#include "../Math/GraphRasterizer.h"
#include "../Util/InputScenarioRunner.h"
#include "../Util/PerformanceProfiler.h"
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
    pendingWindowSize = std::make_pair(width, height);
}

void Application::run()
{
    input->setInputHandlers({shared_from_this()});
    auto inputScenarioRunner = InputScenarioRunner::fromEnvironment();
    if (inputScenarioRunner)
    {
        std::cout << "[InputScenarioRunner] Scripted input enabled via GRAPHX_INPUT_SCRIPT.\n";
    }

    while (!window->shouldClose())
    {
        if (inputScenarioRunner && inputScenarioRunner->isActive())
        {
            GRAPHX_PROFILE_SCOPE("main.scriptedInput");
            inputScenarioRunner->tick(
                [this](const double dx, const double dy) { onCursorDrag(dx, dy); },
                [this](const double offset) { onMouseScrolled(offset); },
                [this](const int width, const int height) { onWindowSizeChanged(width, height); }
            );
        }

        {
            GRAPHX_PROFILE_SCOPE("main.pollAsyncStates");
            computeEngine->pollAsyncStates();
        }

        applyPendingWindowSizeChange();

        {
            GRAPHX_PROFILE_SCOPE("main.render");
            renderer->clear();
            renderer->draw();
        }

        GRAPHX_PROFILE_FLUSH_IF_DUE();

        window->swapBuffers();
        if (inputScenarioRunner && inputScenarioRunner->shouldCloseOnComplete())
        {
            window->getGlfwWindow()->setShouldClose(true);
        }

        if (window->shouldClose())
        {
            break;
        }

        if (inputScenarioRunner && inputScenarioRunner->isActive())
        {
            glfw::waitEvents(inputScenarioRunner->waitTimeoutSeconds());
        }
        else
        {
            glfw::waitEvents();
        }
    }

    GRAPHX_PROFILE_FLUSH_NOW();
}

void Application::onWindowSizeChanged(const int width, const int height)
{
    GRAPHX_PROFILE_SCOPE("app.onWindowSizeChanged");
    pendingWindowSize = std::make_pair(width, height);
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

void Application::onMouseScrolled(double offset)
{
    window->onMouseScrolled(offset);
    renderer->onMouseScrolled(offset);
    sceneManager->onMouseScrolled(offset);
}

void Application::onWindowRefresh()
{
    applyPendingWindowSizeChange();
    renderer->clear();
    renderer->draw();
    window->swapBuffers();
}

void Application::applyPendingWindowSizeChange()
{
    return; // DIAGNOSTIC: skip all resize handling
    if (!pendingWindowSize.has_value())
    {
        return;
    }

    const auto [width, height] = *pendingWindowSize;
    pendingWindowSize.reset();

    if (!isValidFramebufferSize(width, height))
    {
        return;
    }

    if (appliedWindowSize.has_value() && *appliedWindowSize == std::make_pair(width, height))
    {
        return;
    }

    {
        GRAPHX_PROFILE_SCOPE("app.applyWindowSize.window");
        window->onWindowSizeChanged(width, height);
    }
    {
        GRAPHX_PROFILE_SCOPE("app.applyWindowSize.renderer");
        renderer->onWindowSizeChanged(width, height);
    }
    {
        GRAPHX_PROFILE_SCOPE("app.applyWindowSize.sceneManager");
        sceneManager->onWindowSizeChanged(width, height);
    }

    appliedWindowSize = std::make_pair(width, height);
}

bool Application::isValidFramebufferSize(const int width, const int height)
{
    return width > 0 && height > 0;
}

