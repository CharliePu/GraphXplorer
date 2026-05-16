//
// Created by charl on 6/2/2024.
//

#include "Input.h"

#include <iostream>

#include "Window.h"

void UserInputHandler::onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods)
{
}

void UserInputHandler::onCursorDrag(double x, double y)
{
}

void UserInputHandler::onMouseClicked(double x, double y)
{
}

void UserInputHandler::onWindowSizeChanged(int width, int height)
{
}

void UserInputHandler::onTextEntered(unsigned int codepoint)
{
}

void UserInputHandler::onMouseScrolled(double offset)
{
}

void UserInputHandler::onWindowRefresh()
{
}

Input::Input(const std::shared_ptr<Window> &window):
mouseButtonState{glfw::MouseButtonState::Release}, window{window}
{
    auto glfwWindow = window->getGlfwWindow();

    glfwWindow->keyEvent.setCallback([this](glfw::Window &window, glfw::KeyCode key, int scancode,
                                            glfw::KeyState action, glfw::ModifierKeyBit mods) {
        keyCallback(window, key, scancode, action, mods);
    });

    glfwWindow->framebufferSizeEvent.setCallback([this](glfw::Window &window, int width, int height) {
        windowSizeCallback(window, width, height);
    });

    glfwWindow->cursorPosEvent.setCallback([this](glfw::Window &window, double x, double y) {
        cursorPosCallback(window, x, y);
    });

    glfwWindow->mouseButtonEvent.setCallback([this](glfw::Window &window, glfw::MouseButton button,
                                                    glfw::MouseButtonState action, glfw::ModifierKeyBit mods) {
        mouseButtonCallback(window, button, action, mods);
    });

    glfwWindow->charEvent.setCallback([this](glfw::Window &window, unsigned int codepoint) {
        textCallback(window, codepoint);
    });

    glfwWindow->scrollEvent.setCallback([this](glfw::Window &window, double xOffset, double yOffset) {
        scrollCallback(window, xOffset, yOffset);
    });

    glfwWindow->refreshEvent.setCallback([this](glfw::Window &window) {
        windowRefreshCallback(window);
    });
}

void Input::setInputHandlers(const std::shared_ptr<UserInputHandler> &handler)
{
    inputHandler = handler;
}

void Input::keyCallback(glfw::Window &window, glfw::KeyCode key, int scancode, glfw::KeyState action,
                        glfw::ModifierKeyBit mods) const
{
    inputHandler->onKeyPressed(key, scancode, action, mods);
}

void Input::windowSizeCallback(glfw::Window &window, int width, int height) const
{
    (void)window;
    (void)width;
    (void)height;
    inputHandler->onWindowSizeChanged(this->window->getWidth(), this->window->getHeight());
}

void Input::cursorPosCallback(glfw::Window &window, double x, double y)
{
    const auto scale = this->window->getContentScaleFactor();
    x *= scale;
    y *= scale;

    if (mouseButtonState == glfw::MouseButtonState::Press)
    {
        const auto dx = x - mouseX;
        const auto dy = y - mouseY;
        dragDistanceSquared += dx * dx + dy * dy;
        inputHandler->onCursorDrag(dx, dy);
    }

    mouseX = x;
    mouseY = y;
}

void Input::mouseButtonCallback(glfw::Window &window, glfw::MouseButton button, glfw::MouseButtonState action,
                                glfw::ModifierKeyBit mods)
{
    if (button != glfw::MouseButton::Left)
    {
        return;
    }

    if (action == glfw::MouseButtonState::Press)
    {
        pressMouseX = mouseX;
        pressMouseY = mouseY;
        dragDistanceSquared = 0.0;
        mouseButtonState = action;
        return;
    }

    if (mouseButtonState == glfw::MouseButtonState::Press)
    {
        constexpr auto clickThresholdSquared = 25.0;
        if (dragDistanceSquared <= clickThresholdSquared)
        {
            inputHandler->onMouseClicked(mouseX, mouseY);
        }
    }
    mouseButtonState = action;
}

void Input::textCallback(glfw::Window &window, unsigned int codepoint)
{
    inputHandler->onTextEntered(codepoint);
}

void Input::scrollCallback(glfw::Window &window, double xOffset, double yOffset)
{
    inputHandler->onMouseScrolled(yOffset);
}

void Input::windowRefreshCallback(glfw::Window &window)
{
    inputHandler->onWindowRefresh();
}
