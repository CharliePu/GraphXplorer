//
// Created by charl on 6/4/2024.
//

#include "Window.h"

#include <memory>
#include <glfwpp/window.h>

Window::Window(const int width, const int height, const std::string &name):
    glfwContext{glfw::init()},
    window{std::make_shared<glfw::Window>(width, height, name.c_str())}
{
    glfw::makeContextCurrent(*window);

}

bool Window::shouldClose() const
{
    return window->shouldClose();
}

void Window::swapBuffers() const
{
    return window->swapBuffers();
}

int Window::getWidth() const
{
    return std::get<0>(window->getFramebufferSize());
}

int Window::getHeight() const
{
    return std::get<1>(window->getFramebufferSize());
}

double Window::getAspectRatio() const
{
    return static_cast<double>(getWidth()) / getHeight();
}

std::shared_ptr<glfw::Window> Window::getGlfwWindow()
{
    return window;
}

void Window::onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods)
{
    if (key == glfw::KeyCode::Escape && action == glfw::KeyState::Press)
    {
        window->setShouldClose(true);
    }
}
