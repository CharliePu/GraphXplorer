//
// Created by charl on 6/2/2024.
//

#include "Input.h"

#include "Window.h"

void UserInputHandler::onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods)
{
}

void UserInputHandler::onWindowSizeChanged(int width, int height)
{
}

Input::Input(const std::shared_ptr<Window> &window): window(window)
{
    auto glfwWindow = window->getGlfwWindow();

    glfwWindow->keyEvent.setCallback([this](glfw::Window &window, glfw::KeyCode key, int scancode,
                                            glfw::KeyState action, glfw::ModifierKeyBit mods) {
        keyCallback(window, key, scancode, action, mods);
    });

    glfwWindow->framebufferSizeEvent.setCallback([this](glfw::Window &window, int width, int height) {
        windowSizeCallback(window, width, height);
    });
}

void Input::setInputHandlers(const std::shared_ptr<UserInputHandler> &handler)
{
    inputHandler = handler;
}

void Input::keyCallback(glfw::Window &window, glfw::KeyCode key, int scancode, glfw::KeyState action,
                        glfw::ModifierKeyBit mods)
{
    inputHandler->onKeyPressed(key, scancode, action, mods);
}

void Input::windowSizeCallback(glfw::Window &window, int width, int height)
{
    inputHandler->onWindowSizeChanged(width, height);
}
