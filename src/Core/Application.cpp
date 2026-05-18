//
// Created by charl on 6/2/2024.
//

#include "Application.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <variant>

#include "Window.h"
#include "../App/FramePipeline.h"
#include "../App/InteractionController.h"
#include "../Render/Renderer.h"

#include <glad/glad.h>

#include "../Util/InputScenarioRunner.h"
#include "../Util/PerformanceProfiler.h"
#include "../Util/PipelineLog.h"

namespace
{
using Clock = std::chrono::steady_clock;

constexpr auto ResizePresentGuardSettleWindow = std::chrono::milliseconds{250};
constexpr auto ResizePresentGuardMaxDuration = std::chrono::milliseconds{1000};
constexpr auto ResizePresentGuardRetryDelay = std::chrono::milliseconds{4};
constexpr auto ResizePresentFenceTimeout = 10'000'000ULL;
constexpr auto ResizePresentGuardRequiredPresents = 3;
constexpr auto ResizePresentGuardTimeoutsBeforeFinish = 3;

[[nodiscard]] double millisecondsBetween(const Clock::time_point begin, const Clock::time_point end)
{
    if (begin == Clock::time_point{})
    {
        return -1.0;
    }
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

[[nodiscard]] double millisecondsSince(const Clock::time_point begin)
{
    return millisecondsBetween(begin, Clock::now());
}

[[nodiscard]] bool isSlowFrameBoundary(const double deltaMs)
{
    return deltaMs >= 50.0;
}

std::string normalizedUpper(std::string value)
{
    std::ranges::transform(value, value.begin(), [](const unsigned char ch)
    {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

void appendCoalescedInputEvent(std::vector<gx::InputEvent> &events, gx::InputEvent event)
{
    if (std::holds_alternative<gx::ViewportChangedEvent>(event)
        && !events.empty()
        && std::holds_alternative<gx::ViewportChangedEvent>(events.back()))
    {
        events.back() = std::move(event);
        return;
    }

    events.push_back(std::move(event));
}

void mergeRequestBatch(gx::MainThreadRequestBatch &target, gx::MainThreadRequestBatch next)
{
    if (next.resize)
    {
        target.resize = next.resize;
    }
    for (auto &event : next.inputEvents)
    {
        appendCoalescedInputEvent(target.inputEvents, std::move(event));
    }
    std::move(
        next.captures.begin(),
        next.captures.end(),
        std::back_inserter(target.captures));
    target.frameWake = target.frameWake || next.frameWake;
    target.refreshRequested = target.refreshRequested || next.refreshRequested;
    target.closeRequested = target.closeRequested || next.closeRequested;
    target.coalescedResizeRequests += next.coalescedResizeRequests;
    target.coalescedViewportEvents += next.coalescedViewportEvents;
    if (!next.wakeReason.empty())
    {
        target.wakeReason = std::move(next.wakeReason);
    }
}
}

Application::Application(const int width, const int height, const std::string &name) :
    name{name},
    window{std::make_shared<Window>(width, height, name)},
    renderer{std::make_shared<Renderer>(reinterpret_cast<GLADloadproc>(glfw::getProcAddress))},
    input{std::make_shared<Input>(window)},
    framePipeline{std::make_shared<gx::FramePipeline>()},
    interactionController{std::make_shared<gx::InteractionController>()}
{
    PipelineLog::init();
    frameWakeTimer = std::jthread{[this](const std::stop_token stopToken)
    {
        frameWakeTimerLoop(stopToken);
    }};
    requestMailbox.setWakePoster([] { glfw::postEmptyEvent(); });
    framePipeline->setFrameWakeCallback([this]
    {
        logPostedWake("tileComplete", requestMailbox.submitFrameWake("tileComplete"));
    });
    PipelineLog::log(
        "main.presentGuard settleMs=%lld fenceTimeoutNs=%llu requiredPresents=%d",
        static_cast<long long>(ResizePresentGuardSettleWindow.count()),
        static_cast<unsigned long long>(ResizePresentFenceTimeout),
        ResizePresentGuardRequiredPresents);
    renderer->setResourceManager(&framePipeline->renderResources());
    const auto framebufferWidth = window->getWidth();
    const auto framebufferHeight = window->getHeight();
    const auto devicePixelRatio = window->getContentScaleFactor();
    logPostedWake("initialResize", requestMailbox.submitResize(framebufferWidth, framebufferHeight, "initialResize"));
    {
        GRAPHX_PROFILE_SCOPE("app.initialFrame");
        latestFrameSnapshot = framePipeline->process(gx::ViewportChangedEvent{
            Interval{-20.0, 20.0},
            Interval{-20.0, 20.0},
            framebufferWidth,
            framebufferHeight,
            devicePixelRatio
        });
    }
}

Application::~Application()
{
    requestMailbox.setWakePostingEnabled(false);
    if (framePipeline)
    {
        framePipeline->setFrameWakeCallback({});
    }
    stopFrameWakeTimer();
}

void Application::run()
{
    input->setInputHandlers({shared_from_this()});
    auto inputScenarioRunner = InputScenarioRunner::fromEnvironment();
    if (inputScenarioRunner)
    {
        std::cout << "[InputScenarioRunner] Scripted input enabled via GRAPHX_INPUT_SCRIPT.\n";
    }

    while (!window->shouldClose())
    {
        const auto loopStart = Clock::now();
        ++frameCounter;
        const auto frameDeltaMs = millisecondsBetween(lastLoopStart, loopStart);
        lastLoopStart = loopStart;

        const auto pumpStart = Clock::now();
        const auto gapBeforePumpMs = millisecondsBetween(lastEventPumpEnd, pumpStart);
        const auto scenarioActiveBeforePump = inputScenarioRunner && inputScenarioRunner->isActive();
        const auto immediateWorkBeforePump = requestMailbox.hasPendingWork()
            || framePipeline->pendingCompletionCount() > 0;
        if (scenarioActiveBeforePump)
        {
            scheduleFrameWakeAfter(std::chrono::duration<double>{inputScenarioRunner->waitTimeoutSeconds()});
        }

        auto eventMode = immediateWorkBeforePump ? "poll" : "wait";
        if (immediateWorkBeforePump)
        {
            GRAPHX_PROFILE_SCOPE("main.pollEvents.preFrame");
            glfw::pollEvents();
        }
        else
        {
            GRAPHX_PROFILE_SCOPE("main.waitEvents.preFrame");
            glfw::waitEvents();
        }

        const auto pumpEnd = Clock::now();
        const auto eventPumpMs = millisecondsBetween(pumpStart, pumpEnd);
        lastEventPumpEnd = pumpEnd;
        auto requests = requestMailbox.drain();
        const auto wokeForFrame = requests.frameWake;
        const auto pendingRuntimeCompletionsAfterPump = framePipeline->pendingCompletionCount();
        const auto inFlightAfterPump = framePipeline->inFlightCount();
        const auto scenarioActiveAfterPump = inputScenarioRunner && inputScenarioRunner->isActive();
        auto detailedLoopLogging = pumpEnd < detailedLoopLoggingUntil
            || isSlowFrameBoundary(frameDeltaMs)
            || eventPumpMs >= 25.0
            || gapBeforePumpMs >= 50.0;
        if (detailedLoopLogging || frameCounter % 120 == 1)
        {
            PipelineLog::log(
                "main.loop.events frame=%llu phase=pre mode=%s durationMs=%.3f gapBeforePumpMs=%.3f immediateWorkBefore=%d wake=%d pendingEvents=%zu pendingResize=%d runtimePending=%zu runtimeInFlight=%zu scenarioBefore=%d scenarioAfter=%d",
                static_cast<unsigned long long>(frameCounter),
                eventMode,
                eventPumpMs,
                gapBeforePumpMs,
                immediateWorkBeforePump ? 1 : 0,
                wokeForFrame ? 1 : 0,
                requests.inputEvents.size(),
                requests.resize.has_value() ? 1 : 0,
                pendingRuntimeCompletionsAfterPump,
                inFlightAfterPump,
                scenarioActiveBeforePump ? 1 : 0,
                scenarioActiveAfterPump ? 1 : 0);
        }

        if (window->shouldClose())
        {
            break;
        }

        if (pumpEnd < detailedLoopLoggingUntil)
        {
            detailedLoopLogging = true;
        }
        if (detailedLoopLogging || frameCounter % 120 == 1)
        {
            PipelineLog::log(
                "main.loop.begin frame=%llu frameDeltaMs=%.3f pendingEvents=%zu pendingResize=%d framebuffer=%dx%d",
                static_cast<unsigned long long>(frameCounter),
                frameDeltaMs,
                requests.inputEvents.size(),
                requests.resize.has_value() ? 1 : 0,
                window->getWidth(),
                window->getHeight());
        }

        if (inputScenarioRunner && inputScenarioRunner->isActive())
        {
            GRAPHX_PROFILE_SCOPE("main.scriptedInput");
            inputScenarioRunner->tick(
                [this](const double dx, const double dy) { onCursorDrag(dx, dy); },
                [this](const double offset) { onMouseScrolled(offset); },
                [this](const int width, const int height) { requestWindowSize(width, height); },
                [this](const std::string &key, const std::string &state) { onScenarioKey(key, state); },
                [this](const std::string &path) { requestFrameCapture(path); },
                [this](const std::string &expression) { enqueueFrameEvent(gx::FormulaInputEvent{expression}); },
                [this](const std::string &text)
                {
                    for (const auto ch : text)
                    {
                        onTextEntered(static_cast<unsigned char>(ch));
                    }
                },
                [this](const double x, const double y) { onMouseClicked(x, y); },
                [this] { logPostedWake("scenarioClose", requestMailbox.submitClose("scenarioClose")); }
            );
            mergeRequestBatch(requests, requestMailbox.drain());
            if (inputScenarioRunner->isActive())
            {
                scheduleFrameWakeAfter(std::chrono::duration<double>{inputScenarioRunner->waitTimeoutSeconds()});
            }
        }

        {
            GRAPHX_PROFILE_SCOPE("main.applyWindowSizeRequest");
            if (requests.resize)
            {
                (void)applyWindowSizeRequest(*requests.resize, requests.inputEvents);
            }
        }
        auto processedInputFrame = false;
        {
            GRAPHX_PROFILE_SCOPE("main.processFrameEvents");
            processedInputFrame = processFrameEvents(requests.inputEvents);
        }
        auto processedRuntimeFrame = false;
        if (!processedInputFrame
            && (wokeForFrame || pendingRuntimeCompletionsAfterPump > 0 || framePipeline->pendingCompletionCount() > 0))
        {
            GRAPHX_PROFILE_SCOPE("main.renderTickProcess");
            processFrameEvent(gx::RenderTickEvent{});
            processedRuntimeFrame = true;
        }
        if (framePipeline->pendingCompletionCount() > 0)
        {
            logPostedWake("pendingCompletions", requestMailbox.submitFrameWake("pendingCompletions"));
        }

        if (requests.closeRequested || (inputScenarioRunner && inputScenarioRunner->shouldCloseOnComplete()))
        {
            window->getGlfwWindow()->setShouldClose(true);
        }

        if (!processedInputFrame && !processedRuntimeFrame && requests.captures.empty() && !requests.refreshRequested)
        {
            if (detailedLoopLogging)
            {
                PipelineLog::log(
                    "main.loop.frame.skip frame=%llu reason=noFrameWork pendingEvents=%zu pendingResize=%d runtimePending=%zu runtimeInFlight=%zu",
                    static_cast<unsigned long long>(frameCounter),
                    requests.inputEvents.size(),
                    requests.resize.has_value() ? 1 : 0,
                    framePipeline->pendingCompletionCount(),
                    framePipeline->inFlightCount());
            }
            GRAPHX_PROFILE_FLUSH_IF_DUE();
            continue;
        }

        auto renderMs = 0.0;
        auto renderProgress = gx::RenderProgress{};
        const auto renderStart = Clock::now();
        {
            GRAPHX_PROFILE_SCOPE("main.render");
            renderer->clear();
            if (latestFrameSnapshot)
            {
                renderProgress = renderer->draw(std::span<const gx::DrawCommand>{
                    latestFrameSnapshot->drawCommands.data(),
                    latestFrameSnapshot->drawCommands.size()
                }, framePipeline->renderUploadBudget());
            }
            for (const auto &capturePath : requests.captures)
            {
                const auto saved = renderer->saveBackbufferPng(capturePath);
                std::cout << "[FrameCapture] " << (saved ? "Saved " : "Failed ")
                    << capturePath.string() << "\n";
            }
        }
        renderMs = millisecondsBetween(renderStart, Clock::now());
        if (detailedLoopLogging || renderMs >= 16.0)
        {
            PipelineLog::log(
                "main.loop.render frame=%llu durationMs=%.3f commands=%zu",
                static_cast<unsigned long long>(frameCounter),
                renderMs,
                latestFrameSnapshot ? latestFrameSnapshot->drawCommands.size() : size_t{0});
        }

        const auto plannedTextureUploads = latestFrameSnapshot
            && !latestFrameSnapshot->uploadPlan.textureUploads.empty();
        const auto uploadBudgetLeftVisibleWork = latestFrameSnapshot
            && latestFrameSnapshot->uploadPlan.budgetExhausted
            && renderProgress.regionUploadsThisFrame > 0;
        if (renderProgress.needsFollowupFrame() || plannedTextureUploads || uploadBudgetLeftVisibleWork)
        {
            if (detailedLoopLogging || renderProgress.needsFollowupFrame() || uploadBudgetLeftVisibleWork)
            {
                PipelineLog::log(
                    "main.loop.renderUpload.progress frame=%llu uploaded=%zu bytes=%zu pending=%zu planned=%zu budgetExhausted=%d",
                    static_cast<unsigned long long>(frameCounter),
                    renderProgress.regionUploadsThisFrame,
                    renderProgress.regionUploadBytesThisFrame,
                    renderProgress.pendingRegionUploadsAfterFrame,
                    latestFrameSnapshot ? latestFrameSnapshot->uploadPlan.textureUploads.size() : size_t{0},
                    latestFrameSnapshot && latestFrameSnapshot->uploadPlan.budgetExhausted ? 1 : 0);
            }
            logPostedWake("renderUpload", requestMailbox.submitFrameWake("renderUpload"));
        }

        GRAPHX_PROFILE_FLUSH_IF_DUE();

        const auto presentStart = Clock::now();
        const auto logPresentPhases = detailedLoopLogging
            || resizePresentGuardActive(presentStart)
            || presentStart < detailedLoopLoggingUntil;
        if (logPresentPhases)
        {
            PipelineLog::log(
                "main.loop.present.begin frame=%llu framebuffer=%dx%d renderMs=%.3f commands=%zu guard=%d",
                static_cast<unsigned long long>(frameCounter),
                window->getWidth(),
                window->getHeight(),
                renderMs,
                latestFrameSnapshot ? latestFrameSnapshot->drawCommands.size() : size_t{0},
                resizePresentGuardActive(presentStart) ? 1 : 0);
        }

        auto presentGuardWaitMs = 0.0;
        auto presentGuardAction = "none";
        if (!prepareResizeGuardedPresent(detailedLoopLogging, presentGuardWaitMs, presentGuardAction))
        {
            enableDetailedLoopLogging(Clock::now());
            PipelineLog::log(
                "main.loop.present.skip frame=%llu reason=%s guardWaitMs=%.3f framebuffer=%dx%d",
                static_cast<unsigned long long>(frameCounter),
                presentGuardAction,
                presentGuardWaitMs,
                window->getWidth(),
                window->getHeight());
            GRAPHX_PROFILE_FLUSH_IF_DUE();
            continue;
        }

        if (logPresentPhases)
        {
            PipelineLog::log(
                "main.loop.swap.begin frame=%llu framebuffer=%dx%d",
                static_cast<unsigned long long>(frameCounter),
                window->getWidth(),
                window->getHeight());
        }

        const auto swapStart = Clock::now();
        {
            GRAPHX_PROFILE_SCOPE("main.swapBuffers");
            window->swapBuffers();
        }
        const auto swapEnd = Clock::now();
        const auto swapMs = millisecondsBetween(swapStart, swapEnd);
        recordResizeGuardedSwap(swapEnd, swapMs);
        if (swapMs >= 50.0)
        {
            enableDetailedLoopLogging(Clock::now());
            PipelineLog::log(
                "main.loop.present.slow frame=%llu durationMs=%.3f",
                static_cast<unsigned long long>(frameCounter),
                swapMs);
        }
        if (logPresentPhases || swapMs >= 16.0)
        {
            PipelineLog::log(
                "main.loop.swap.end frame=%llu durationMs=%.3f",
                static_cast<unsigned long long>(frameCounter),
                swapMs);
        }
        if (detailedLoopLogging || swapMs >= 16.0)
        {
            PipelineLog::log(
                "main.loop.swap frame=%llu durationMs=%.3f guardAction=%s guardWaitMs=%.3f",
                static_cast<unsigned long long>(frameCounter),
                swapMs,
                presentGuardAction,
                presentGuardWaitMs);
        }

        if (window->shouldClose())
        {
            break;
        }
    }

    GRAPHX_PROFILE_FLUSH_NOW();
    requestMailbox.setWakePostingEnabled(false);
    if (framePipeline)
    {
        framePipeline->setFrameWakeCallback({});
    }
    stopFrameWakeTimer();
    PipelineLog::shutdown();
}

void Application::onWindowSizeChanged(const int width, const int height)
{
    GRAPHX_PROFILE_SCOPE("app.onWindowSizeChanged");
    const auto now = Clock::now();
    logPostedWake("resize", requestMailbox.submitResize(width, height, "resize"));
    enableDetailedLoopLogging(now);
    PipelineLog::log(
        "app.resize.callback frame=%llu size=%dx%d currentFramebuffer=%dx%d pendingEvents=%zu",
        static_cast<unsigned long long>(frameCounter),
        width,
        height,
        window->getWidth(),
        window->getHeight(),
        requestMailbox.pendingInputCount());
}

void Application::onKeyPressed(const glfw::KeyCode key, const int scancode, const glfw::KeyState action,
                               const glfw::ModifierKeyBit mods)
{
    const auto events = interactionController->handleKey(key, action, framePipeline->state());
    if (events.empty())
    {
        window->onKeyPressed(key, scancode, action, mods);
        return;
    }

    for (const auto &event : events)
    {
        enqueueFrameEvent(event);
    }
}

void Application::onCursorDrag(double x, double y)
{
    for (const auto &event : interactionController->handleDrag(x, y, framePipeline->state()))
    {
        enqueueFrameEvent(event);
    }
}

void Application::onMouseClicked(double x, double y)
{
    for (const auto &event : interactionController->handleClick(x, y, framePipeline->state()))
    {
        enqueueFrameEvent(event);
    }
}

void Application::onTextEntered(unsigned int codepoint)
{
    for (const auto &event : interactionController->handleText(codepoint, framePipeline->state()))
    {
        enqueueFrameEvent(event);
    }
}

void Application::onMouseScrolled(double offset)
{
    for (const auto &event : interactionController->handleScroll(offset, framePipeline->state()))
    {
        enqueueFrameEvent(event);
    }
}

void Application::onWindowRefresh()
{
    logPostedWake("refresh", requestMailbox.submitRefresh("refresh"));
}

size_t Application::applyWindowSizeRequest(const gx::FramebufferResizeRequest &request,
                                           std::vector<gx::InputEvent> &events)
{
    const auto requestedWidth = request.width;
    const auto requestedHeight = request.height;

    if (!isValidFramebufferSize(requestedWidth, requestedHeight))
    {
        return 0;
    }

    const auto framebufferWidth = requestedWidth;
    const auto framebufferHeight = requestedHeight;
    const auto devicePixelRatio = window->getContentScaleFactor();
    if (!isValidFramebufferSize(framebufferWidth, framebufferHeight))
    {
        return 0;
    }

    const auto actualSize = std::make_pair(framebufferWidth, framebufferHeight);
    if (appliedWindowSize.has_value() && *appliedWindowSize == actualSize)
    {
        return 0;
    }
    const auto previousSize = appliedWindowSize;
    const auto rendererResizeStart = Clock::now();
    {
        GRAPHX_PROFILE_SCOPE("app.applyWindowSize.renderer");
        renderer->onWindowSizeChanged(framebufferWidth, framebufferHeight);
    }
    const auto rendererResizeMs = millisecondsSince(rendererResizeStart);
    const auto eventsBeforeResize = events.size();
    for (const auto &event : interactionController->handleResize(
             framebufferWidth,
             framebufferHeight,
             framePipeline->state(),
             devicePixelRatio))
    {
        appendCoalescedInputEvent(events, event);
    }
    appliedWindowSize = actualSize;
    markResizePresentGuard(Clock::now());
    const auto enqueuedEvents = events.size() - eventsBeforeResize;
    enableDetailedLoopLogging(Clock::now());
    PipelineLog::log(
        "app.resize.apply frame=%llu size=%dx%d previous=%dx%d dpr=%.3f rendererMs=%.3f enqueuedEvents=%zu",
        static_cast<unsigned long long>(frameCounter),
        framebufferWidth,
        framebufferHeight,
        previousSize ? previousSize->first : 0,
        previousSize ? previousSize->second : 0,
        devicePixelRatio,
        rendererResizeMs,
        enqueuedEvents);
    return enqueuedEvents;
}

void Application::requestWindowSize(const int width, const int height)
{
    if (!isValidFramebufferSize(width, height))
    {
        return;
    }

    {
        GRAPHX_PROFILE_SCOPE("app.requestWindowSize.window");
        PipelineLog::log(
            "app.resize.request frame=%llu size=%dx%d currentFramebuffer=%dx%d",
            static_cast<unsigned long long>(frameCounter),
            width,
            height,
            window->getWidth(),
            window->getHeight());
        window->onWindowSizeChanged(width, height);
    }
    logPostedWake(
        "resizeRequest",
        requestMailbox.submitResize(window->getWidth(), window->getHeight(), "resizeRequest"));
}

void Application::enqueueFrameEvent(const gx::InputEvent &event)
{
    logPostedWake("input", requestMailbox.submitInput(event, "input"));
}

bool Application::processFrameEvents(const std::vector<gx::InputEvent> &events)
{
    if (events.empty())
    {
        return false;
    }

    for (const auto &event : events)
    {
        processFrameEvent(event);
    }
    return true;
}

void Application::processFrameEvent(const gx::InputEvent &event)
{
    GRAPHX_PROFILE_SCOPE("app.processFrameEvent");
    latestFrameSnapshot = framePipeline->process(event);
}

void Application::onScenarioKey(const std::string &keyName, const std::string &stateName)
{
    const auto key = keyCodeForScenarioName(keyName);
    if (!key)
    {
        std::cerr << "[InputScenarioRunner] Unknown key: " << keyName << "\n";
        return;
    }

    const auto state = stateName == "release" ? glfw::KeyState::Release : glfw::KeyState::Press;
    onKeyPressed(*key, 0, state, static_cast<glfw::ModifierKeyBit>(0));
}

void Application::requestFrameCapture(const std::string &path)
{
    logPostedWake("capture", requestMailbox.submitCapture(std::filesystem::path{path}, "capture"));
}

std::optional<glfw::KeyCode> Application::keyCodeForScenarioName(const std::string &keyName)
{
    const auto key = normalizedUpper(keyName);
    if (key == "D") return glfw::KeyCode::D;
    if (key == "H") return glfw::KeyCode::H;
    if (key == "I") return glfw::KeyCode::I;
    if (key == "ENTER") return glfw::KeyCode::Enter;
    if (key == "BACKSPACE") return glfw::KeyCode::Backspace;
    if (key == "DELETE" || key == "DEL") return glfw::KeyCode::Delete;
    if (key == "LEFT") return glfw::KeyCode::Left;
    if (key == "RIGHT") return glfw::KeyCode::Right;
    if (key == "HOME") return glfw::KeyCode::Home;
    if (key == "END") return glfw::KeyCode::End;
    if (key == "ESC" || key == "ESCAPE") return glfw::KeyCode::Escape;
    return std::nullopt;
}

bool Application::isValidFramebufferSize(const int width, const int height)
{
    return width > 0 && height > 0;
}

void Application::enableDetailedLoopLogging(const std::chrono::steady_clock::time_point now)
{
    detailedLoopLoggingUntil = std::max(detailedLoopLoggingUntil, now + std::chrono::seconds{8});
}

void Application::logPostedWake(const char *reason, const bool posted) const
{
    if (!posted)
    {
        return;
    }

    PipelineLog::log(
        "main.loop.wake.post reason=%s pendingRuntime=%zu inFlight=%zu",
        reason ? reason : "unknown",
        framePipeline ? framePipeline->pendingCompletionCount() : size_t{0},
        framePipeline ? framePipeline->inFlightCount() : size_t{0});
}

void Application::markResizePresentGuard(const std::chrono::steady_clock::time_point now)
{
    resizePresentGuardEnabled = true;
    resizePresentGuardStarted = now;
    resizePresentGuardUntil = now + ResizePresentGuardSettleWindow;
    resizePresentGuardSuccessfulPresents = 0;
    resizePresentGuardConsecutiveTimeouts = 0;
    PipelineLog::log(
        "main.presentGuard.begin frame=%llu settleMs=%lld requiredPresents=%d",
        static_cast<unsigned long long>(frameCounter),
        static_cast<long long>(ResizePresentGuardSettleWindow.count()),
        ResizePresentGuardRequiredPresents);
}

bool Application::resizePresentGuardActive(const std::chrono::steady_clock::time_point now) const
{
    if (!resizePresentGuardEnabled)
    {
        return false;
    }

    if (now >= resizePresentGuardUntil
        && millisecondsBetween(resizePresentGuardStarted, now)
            >= static_cast<double>(ResizePresentGuardMaxDuration.count()))
    {
        return false;
    }

    return now < resizePresentGuardUntil
        || resizePresentGuardSuccessfulPresents < ResizePresentGuardRequiredPresents;
}

bool Application::prepareResizeGuardedPresent(const bool detailedLoopLogging,
                                              double &guardWaitMs,
                                              const char *&guardAction)
{
    guardWaitMs = 0.0;
    guardAction = "none";

    const auto now = Clock::now();
    if (!resizePresentGuardActive(now))
    {
        resizePresentGuardEnabled = false;
        return true;
    }

    const auto finishFallback = [this, &guardWaitMs, &guardAction](const char *action)
    {
        const auto finishStart = Clock::now();
        {
            GRAPHX_PROFILE_SCOPE("main.resizePresentGuard.finish");
            glFinish();
        }
        guardWaitMs += millisecondsBetween(finishStart, Clock::now());
        guardAction = action;
        resizePresentGuardConsecutiveTimeouts = 0;
        if (guardWaitMs >= 50.0)
        {
            enableDetailedLoopLogging(Clock::now());
        }
        return true;
    };

    const auto syncUnavailable = glFenceSync == nullptr
        || glClientWaitSync == nullptr
        || glDeleteSync == nullptr;
    if (syncUnavailable)
    {
        return finishFallback("finishNoSync");
    }

    GLsync fence = nullptr;
    {
        GRAPHX_PROFILE_SCOPE("main.resizePresentGuard.fence");
        fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
    if (!fence)
    {
        return finishFallback("finishFenceCreateFailed");
    }

    const auto waitStart = Clock::now();
    const auto waitResult = glClientWaitSync(
        fence,
        GL_SYNC_FLUSH_COMMANDS_BIT,
        ResizePresentFenceTimeout);
    guardWaitMs = millisecondsBetween(waitStart, Clock::now());
    glDeleteSync(fence);

    if (waitResult == GL_ALREADY_SIGNALED || waitResult == GL_CONDITION_SATISFIED)
    {
        resizePresentGuardConsecutiveTimeouts = 0;
        guardAction = "fenceReady";
        if (detailedLoopLogging || guardWaitMs >= 4.0)
        {
            PipelineLog::log(
                "main.presentGuard.ready frame=%llu waitMs=%.3f completed=%d",
                static_cast<unsigned long long>(frameCounter),
                guardWaitMs,
                resizePresentGuardSuccessfulPresents);
        }
        return true;
    }

    if (waitResult == GL_TIMEOUT_EXPIRED)
    {
        ++resizePresentGuardConsecutiveTimeouts;
        const auto guardAgeMs = millisecondsSince(resizePresentGuardStarted);
        if (resizePresentGuardConsecutiveTimeouts >= ResizePresentGuardTimeoutsBeforeFinish
            || guardAgeMs >= static_cast<double>(ResizePresentGuardMaxDuration.count()))
        {
            return finishFallback("finishAfterFenceTimeouts");
        }

        guardAction = "fenceTimeout";
        scheduleFrameWakeAfter(ResizePresentGuardRetryDelay);
        return false;
    }

    return finishFallback("finishFenceWaitFailed");
}

void Application::recordResizeGuardedSwap(const std::chrono::steady_clock::time_point now, const double swapMs)
{
    if (!resizePresentGuardEnabled)
    {
        return;
    }

    if (swapMs >= 50.0)
    {
        resizePresentGuardUntil = now + ResizePresentGuardSettleWindow;
        resizePresentGuardSuccessfulPresents = 0;
        resizePresentGuardConsecutiveTimeouts = 0;
        PipelineLog::log(
            "main.presentGuard.extend frame=%llu reason=slowSwap swapMs=%.3f",
            static_cast<unsigned long long>(frameCounter),
            swapMs);
        return;
    }

    ++resizePresentGuardSuccessfulPresents;
    if (resizePresentGuardActive(now))
    {
        return;
    }

    resizePresentGuardEnabled = false;
    PipelineLog::log(
        "main.presentGuard.end frame=%llu completedPresents=%d",
        static_cast<unsigned long long>(frameCounter),
        resizePresentGuardSuccessfulPresents);
}

void Application::scheduleFrameWakeAt(const std::chrono::steady_clock::time_point wakeAt)
{
    if (wakeAt == std::chrono::steady_clock::time_point{})
    {
        return;
    }

    {
        std::lock_guard lock(frameWakeTimerMutex);
        if (scheduledFrameWake.has_value() && *scheduledFrameWake <= wakeAt)
        {
            return;
        }
        scheduledFrameWake = wakeAt;
    }
    frameWakeTimerCv.notify_all();
}

void Application::scheduleFrameWakeAfter(const std::chrono::duration<double> delay)
{
    if (delay <= std::chrono::duration<double>::zero())
    {
        logPostedWake("timerImmediate", requestMailbox.submitFrameWake("timerImmediate"));
        return;
    }
    scheduleFrameWakeAt(Clock::now() + std::chrono::duration_cast<Clock::duration>(delay));
}

void Application::frameWakeTimerLoop(const std::stop_token stopToken)
{
    std::unique_lock lock(frameWakeTimerMutex);
    while (!stopToken.stop_requested())
    {
        if (!scheduledFrameWake.has_value())
        {
            frameWakeTimerCv.wait(lock, stopToken, [this]
            {
                return scheduledFrameWake.has_value();
            });
            continue;
        }

        const auto wakeAt = *scheduledFrameWake;
        const auto wokeEarly = frameWakeTimerCv.wait_until(lock, stopToken, wakeAt, [this, wakeAt]
        {
            return !scheduledFrameWake.has_value() || *scheduledFrameWake != wakeAt;
        });
        if (stopToken.stop_requested())
        {
            break;
        }
        if (wokeEarly)
        {
            continue;
        }
        if (!scheduledFrameWake.has_value() || *scheduledFrameWake > Clock::now())
        {
            continue;
        }

        scheduledFrameWake.reset();
        lock.unlock();
        logPostedWake("timer", requestMailbox.submitFrameWake("timer"));
        lock.lock();
    }
}

void Application::stopFrameWakeTimer()
{
    if (!frameWakeTimer.joinable())
    {
        return;
    }

    frameWakeTimer.request_stop();
    frameWakeTimerCv.notify_all();
    frameWakeTimer.join();
}
