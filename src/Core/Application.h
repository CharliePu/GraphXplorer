//
// Created by charl on 6/2/2024.
//

#ifndef APPLICATION_H
#define APPLICATION_H

#include <memory>

#define GLFW_INCLUDE_NONE
#include <glfwpp/glfwpp.h>

#include "Input.h"


class Window;
class GraphRasterizer;
class ComputeEngine;
class SceneManager;
class Renderer;

class Application : public UserInputHandler {
public:
    Application(int width, int height, const std::string &name);

    void run();

    void onWindowSizeChanged(int width, int height) override;

    void onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods) override;

    void onCursorDrag(double x, double y) override;

    void onTextEntered(unsigned int codepoint) override;

private:
    std::string name;

    std::shared_ptr<Window> window;
    std::shared_ptr<Renderer> renderer;
    std::shared_ptr<Input> input;
    std::shared_ptr<ComputeEngine> computeEngine;
    std::shared_ptr<SceneManager> sceneManager;
};


#endif //APPLICATION_H
