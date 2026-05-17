//
// Created by charl on 6/2/2024.
//

#ifndef APPLICATION_H
#define APPLICATION_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <optional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <glfwpp/glfwpp.h>

#include <filesystem>

#include "Input.h"
#include "MainThreadRequestMailbox.h"
#include "../App/AppState.h"
#include "../Util/Contracts.h"


namespace glfw
{
    class KeyCode;
}

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
    ~Application() override;

    void run();

    void onWindowSizeChanged(int width, int height) override;

    void onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods) override;

    void onCursorDrag(double x, double y) override;

    void onMouseClicked(double x, double y) override;

    void onTextEntered(unsigned int codepoint) override;

    void onMouseScrolled(double offset) override;

    void onWindowRefresh() override;

private:
    size_t applyWindowSizeRequest(const gx::FramebufferResizeRequest &request,
                                  std::vector<gx::InputEvent> &events);
    void requestWindowSize(int width, int height);
    void enqueueFrameEvent(const gx::InputEvent &event);
    [[nodiscard]] bool processFrameEvents(const std::vector<gx::InputEvent> &events);
    void processFrameEvent(const gx::InputEvent &event);
    void onScenarioKey(const std::string &keyName, const std::string &stateName);
    void requestFrameCapture(const std::string &path);
    [[nodiscard]] static std::optional<glfw::KeyCode> keyCodeForScenarioName(const std::string &keyName);
    static bool isValidFramebufferSize(int width, int height);
    void enableDetailedLoopLogging(std::chrono::steady_clock::time_point now);
    void logPostedWake(const char *reason, bool posted) const;
    void markResizePresentGuard(std::chrono::steady_clock::time_point now);
    [[nodiscard]] bool resizePresentGuardActive(std::chrono::steady_clock::time_point now) const;
    [[nodiscard]] bool prepareResizeGuardedPresent(bool detailedLoopLogging,
                                                   double &guardWaitMs,
                                                   const char *&guardAction);
    void recordResizeGuardedSwap(std::chrono::steady_clock::time_point now, double swapMs);
    void scheduleFrameWakeAt(std::chrono::steady_clock::time_point wakeAt);
    void scheduleFrameWakeAfter(std::chrono::duration<double> delay);
    void frameWakeTimerLoop(std::stop_token stopToken);
    void stopFrameWakeTimer();

    std::string name;

    std::shared_ptr<Window> window;
    std::shared_ptr<Renderer> renderer;
    std::shared_ptr<Input> input;
    std::shared_ptr<gx::FramePipeline> framePipeline;
    std::shared_ptr<gx::InteractionController> interactionController;
    std::optional<gx::FrameSnapshot> latestFrameSnapshot;
    std::optional<std::pair<int, int>> appliedWindowSize;
    uint64_t frameCounter{0};
    std::chrono::steady_clock::time_point lastLoopStart{};
    std::chrono::steady_clock::time_point lastEventPumpEnd{};
    std::chrono::steady_clock::time_point detailedLoopLoggingUntil{};
    std::chrono::steady_clock::time_point resizePresentGuardStarted{};
    std::chrono::steady_clock::time_point resizePresentGuardUntil{};
    gx::MainThreadRequestMailbox requestMailbox{};
    std::optional<std::chrono::steady_clock::time_point> scheduledFrameWake{};
    std::mutex frameWakeTimerMutex;
    std::condition_variable_any frameWakeTimerCv;
    std::jthread frameWakeTimer;
    int resizePresentGuardSuccessfulPresents{0};
    int resizePresentGuardConsecutiveTimeouts{0};
    bool resizePresentGuardEnabled{false};
};


#endif //APPLICATION_H
