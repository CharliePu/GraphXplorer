//
// Created by charl on 6/2/2024.
//

#ifndef APPLICATION_H
#define APPLICATION_H

#include <chrono>
#include <optional>
#include <memory>

#define GLFW_INCLUDE_NONE
#include <glfwpp/glfwpp.h>

#include "Input.h"


namespace glfw
{
    class KeyCode;
}

class ComputeEngine;
class ThreadPool;
class Window;
class GraphRasterizer;
class GraphProcessor;
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

    void onMouseScrolled(double offset) override;

    void onWindowRefresh() override;

private:
    void applyPendingWindowSizeChange();
    void applySettledResizeWork();
    [[nodiscard]] std::optional<double> resizeWaitTimeoutSeconds() const;
    static bool isValidFramebufferSize(int width, int height);

    std::string name;

    std::shared_ptr<Window> window;
    std::shared_ptr<Renderer> renderer;
    std::shared_ptr<Input> input;
    std::shared_ptr<ThreadPool> threadPool;
    std::shared_ptr<ComputeEngine> computeEngine;
    std::shared_ptr<SceneManager> sceneManager;
    std::optional<std::pair<int, int>> pendingWindowSize;
    std::optional<std::pair<int, int>> pendingSettledWindowSize;
    std::optional<std::pair<int, int>> appliedWindowSize;
    std::chrono::steady_clock::time_point lastResizeEventTime{};
};


#endif //APPLICATION_H
