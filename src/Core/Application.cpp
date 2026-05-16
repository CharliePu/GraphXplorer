//
// Created by charl on 6/2/2024.
//

#include "Application.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "Window.h"
#include "../App/FramePipeline.h"
#include "../App/InteractionController.h"
#include "../Render/Renderer.h"

#include <glad/glad.h>

#include "../Util/InputScenarioRunner.h"
#include "../Util/PerformanceProfiler.h"
#include "../Util/PipelineLog.h"

namespace
{
std::string normalizedUpper(std::string value)
{
    std::ranges::transform(value, value.begin(), [](const unsigned char ch)
    {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}
}

Application::Application(const int width, const int height, const std::string &name) :
    name{name},
    window{std::make_shared<Window>(width, height, name)},
    renderer{std::make_shared<Renderer>(reinterpret_cast<GLADloadproc>(glfw::getProcAddress))},
    input{std::make_shared<Input>(window)},
    framePipeline{std::make_shared<gx::FramePipeline>()},
    interactionController{std::make_shared<gx::InteractionController>()}
{
    PipelineLog::init();
    renderer->setResourceManager(&framePipeline->renderResources());
    pendingWindowSize = std::make_pair(width, height);
    latestFrameSnapshot = framePipeline->process(gx::ViewportChangedEvent{
        Interval{-20.0, 20.0},
        Interval{-20.0, 20.0},
        width,
        height
    });
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
                [this](const int width, const int height) { onWindowSizeChanged(width, height); },
                [this](const std::string &key, const std::string &state) { onScenarioKey(key, state); },
                [this](const std::string &path) { requestFrameCapture(path); }
            );
        }

        applyPendingWindowSizeChange();
        processFrameEvent(gx::RenderTickEvent{});

        {
            GRAPHX_PROFILE_SCOPE("main.render");
            renderer->clear();
            if (latestFrameSnapshot)
            {
                renderer->draw(std::span<const gx::DrawCommand>{
                    latestFrameSnapshot->drawCommands.data(),
                    latestFrameSnapshot->drawCommands.size()
                });
            }
            if (pendingCapturePath)
            {
                const auto saved = renderer->saveBackbufferPng(*pendingCapturePath);
                std::cout << "[FrameCapture] " << (saved ? "Saved " : "Failed ")
                    << pendingCapturePath->string() << "\n";
                pendingCapturePath.reset();
            }
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
            glfw::waitEvents(std::min(inputScenarioRunner->waitTimeoutSeconds(), 1.0 / 60.0));
        }
        else
        {
            glfw::waitEvents(1.0 / 60.0);
        }
    }

    GRAPHX_PROFILE_FLUSH_NOW();
    PipelineLog::shutdown();
}

void Application::onWindowSizeChanged(const int width, const int height)
{
    GRAPHX_PROFILE_SCOPE("app.onWindowSizeChanged");
    pendingWindowSize = std::make_pair(width, height);
}

void Application::onKeyPressed(const glfw::KeyCode key, const int scancode, const glfw::KeyState action,
                               const glfw::ModifierKeyBit mods)
{
    (void)scancode;
    (void)mods;
    window->onKeyPressed(key, scancode, action, mods);
    for (const auto &event : interactionController->handleKey(key, action, framePipeline->state()))
    {
        processFrameEvent(event);
    }
}

void Application::onCursorDrag(double x, double y)
{
    for (const auto &event : interactionController->handleDrag(x, y, framePipeline->state()))
    {
        processFrameEvent(event);
    }
}

void Application::onTextEntered(unsigned int codepoint)
{
    for (const auto &event : interactionController->handleText(codepoint, framePipeline->state()))
    {
        processFrameEvent(event);
    }
}

void Application::onMouseScrolled(double offset)
{
    for (const auto &event : interactionController->handleScroll(offset, framePipeline->state()))
    {
        processFrameEvent(event);
    }
}

void Application::onWindowRefresh()
{
    // The refresh event wakes the main loop; rendering stays centralized there.
}

void Application::applyPendingWindowSizeChange()
{
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
    for (const auto &event : interactionController->handleResize(width, height, framePipeline->state()))
    {
        processFrameEvent(event);
    }
    appliedWindowSize = std::make_pair(width, height);
}

void Application::processFrameEvent(const gx::InputEvent &event)
{
    latestFrameSnapshot = framePipeline->process(event);
}

void Application::onScenarioKey(const std::string &keyName, const std::string &stateName)
{
    const auto key = keyCodeForScenarioName(keyName);
    if (!key)
    {
        std::cerr << "[InputScenarioRunner] Unknown key: " << keyName << "\n";
        return;
    }

    const auto state = stateName == "release" ? glfw::KeyState::Release : glfw::KeyState::Press;
    onKeyPressed(*key, 0, state, static_cast<glfw::ModifierKeyBit>(0));
}

void Application::requestFrameCapture(const std::string &path)
{
    pendingCapturePath = std::filesystem::path{path};
}

std::optional<glfw::KeyCode> Application::keyCodeForScenarioName(const std::string &keyName)
{
    const auto key = normalizedUpper(keyName);
    if (key == "D") return glfw::KeyCode::D;
    if (key == "H") return glfw::KeyCode::H;
    if (key == "I") return glfw::KeyCode::I;
    if (key == "ENTER") return glfw::KeyCode::Enter;
    if (key == "BACKSPACE") return glfw::KeyCode::Backspace;
    if (key == "ESC" || key == "ESCAPE") return glfw::KeyCode::Escape;
    return std::nullopt;
}

bool Application::isValidFramebufferSize(const int width, const int height)
{
    return width > 0 && height > 0;
}
