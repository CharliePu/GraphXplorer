//
// Created by charl on 6/4/2024.
//

#ifndef WINDOW_H
#define WINDOW_H
#include <memory>
#include <string>
#include <glfwpp/glfwpp.h>

#include "Input.h"


class Window: public UserInputHandler {
public:
    Window(int width, int height, const std::string &name);

    [[nodiscard]] bool shouldClose() const;

    void swapBuffers() const;

    [[nodiscard]] int getWidth() const;

    [[nodiscard]] int getHeight() const;

    [[nodiscard]] double getAspectRatio() const;

    std::shared_ptr<glfw::Window> getGlfwWindow();

    void onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods) override;

private:
    glfw::GlfwLibrary glfwContext;
    std::shared_ptr<glfw::Window> window;

};



#endif //WINDOW_H
