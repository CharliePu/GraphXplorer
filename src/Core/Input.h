//
// Created by charl on 6/2/2024.
//

#ifndef INPUT_H
#define INPUT_H

#include <memory>
#define GLFW_INCLUDE_NONE
#include <glfwpp/glfwpp.h>

class Window;

class UserInputHandler: public std::enable_shared_from_this<UserInputHandler> {
public:
    virtual ~UserInputHandler() = default;

    virtual void onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods);
    virtual void onWindowSizeChanged(int width, int height);
};

class Input {
public:
    explicit Input(const std::shared_ptr<Window> &window);
    void setInputHandlers(const std::shared_ptr<UserInputHandler> &inputHandler);
private:

    void keyCallback(glfw::Window &window, glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods);
    void windowSizeCallback(glfw::Window &window, int width, int height);

    std::shared_ptr<Window> window;
    std::shared_ptr<UserInputHandler> inputHandler;
};


#endif //INPUT_H
