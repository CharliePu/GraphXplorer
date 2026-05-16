//
// Created by charl on 6/2/2024.
//

#ifndef APPLICATION_H
#define APPLICATION_H

#include <chrono>
#include <optional>
#include <memory>
#include <string>

#define GLFW_INCLUDE_NONE
#include <glfwpp/glfwpp.h>

#include <filesystem>

#include "Input.h"
#include "../App/AppState.h"
#include "../Util/Contracts.h"


namespace glfw
{
    class KeyCode;
}

class ComputeEngine;
class ThreadPool;
class Window;
class Renderer;

namespace gx
{
class FramePipeline;
class InteractionController;
}

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
    void processFrameEvent(const gx::InputEvent &event);
    void onScenarioKey(const std::string &keyName, const std::string &stateName);
    void requestFrameCapture(const std::string &path);
    [[nodiscard]] static std::optional<glfw::KeyCode> keyCodeForScenarioName(const std::string &keyName);
    static bool isValidFramebufferSize(int width, int height);

    std::string name;

    std::shared_ptr<Window> window;
    std::shared_ptr<Renderer> renderer;
    std::shared_ptr<Input> input;
    std::shared_ptr<gx::FramePipeline> framePipeline;
    std::shared_ptr<gx::InteractionController> interactionController;
    std::optional<gx::FrameSnapshot> latestFrameSnapshot;
    std::optional<std::pair<int, int>> pendingWindowSize;
    std::optional<std::pair<int, int>> appliedWindowSize;
    std::optional<std::filesystem::path> pendingCapturePath;
};


#endif //APPLICATION_H
