//
// Created by charl on 6/4/2024.
//

#include "Window.h"

#include <algorithm>
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
    const auto height = getHeight();
    if (height <= 0)
    {
        return 1.0;
    }

    return static_cast<double>(getWidth()) / static_cast<double>(height);
}

double Window::getContentScaleFactor() const
{
    const auto [xScale, yScale] = window->getContentScale();
    return std::max(1.0, static_cast<double>(std::max(xScale, yScale)));
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

void Window::onWindowSizeChanged(const int width, const int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    if (getWidth() == width && getHeight() == height)
    {
        return;
    }

    window->setSize(width, height);
}
