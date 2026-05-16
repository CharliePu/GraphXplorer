#include "InteractionController.h"

#include <cmath>
#include <string>

namespace gx
{
std::vector<InputEvent> InteractionController::handleKey(const glfw::KeyCode key,
                                                         const glfw::KeyState action,
                                                         const AppState &state) const
{
    std::vector<InputEvent> events;
    if (state.formulaInput.active)
    {
        if (key == glfw::KeyCode::Enter && action == glfw::KeyState::Press)
        {
            events.emplace_back(SubmitFormulaInputEvent{});
        }
        else if (key == glfw::KeyCode::Backspace && action != glfw::KeyState::Release)
        {
            events.emplace_back(BackspaceFormulaInputEvent{});
        }
        else if (key == glfw::KeyCode::Escape && action == glfw::KeyState::Press)
        {
            events.emplace_back(CancelFormulaInputEvent{});
        }
        return events;
    }

    if (key == glfw::KeyCode::I && action == glfw::KeyState::Release)
    {
        events.emplace_back(BeginFormulaInputEvent{});
    }
    else if (key == glfw::KeyCode::D && action == glfw::KeyState::Press)
    {
        events.emplace_back(DebugToggleEvent{!state.debug});
    }
    else if (key == glfw::KeyCode::H && action == glfw::KeyState::Press)
    {
        events.emplace_back(ViewportChangedEvent{
            Interval{-20.0, 20.0},
            Interval{-20.0, 20.0},
            state.framebufferWidth,
            state.framebufferHeight
        });
    }

    return events;
}

std::vector<InputEvent> InteractionController::handleText(const unsigned int codepoint,
                                                          const AppState &state) const
{
    if (!state.formulaInput.active || !printableAscii(codepoint))
    {
        return {};
    }
    return {AppendFormulaInputEvent{std::string{static_cast<char>(codepoint)}}};
}

std::vector<InputEvent> InteractionController::handleDrag(const double dx,
                                                          const double dy,
                                                          const AppState &state) const
{
    if (state.formulaInput.active || state.framebufferWidth <= 0 || state.framebufferHeight <= 0)
    {
        return {};
    }

    const auto worldDx = -dx / static_cast<double>(state.framebufferWidth) * state.xRange.size();
    const auto worldDy = dy / static_cast<double>(state.framebufferHeight) * state.yRange.size();
    return {ViewportChangedEvent{
        Interval{state.xRange.lower + worldDx, state.xRange.upper + worldDx},
        Interval{state.yRange.lower + worldDy, state.yRange.upper + worldDy},
        state.framebufferWidth,
        state.framebufferHeight
    }};
}

std::vector<InputEvent> InteractionController::handleScroll(const double offset,
                                                            const AppState &state) const
{
    if (state.formulaInput.active)
    {
        return {};
    }

    const auto zoom = std::pow(0.9, offset);
    const auto centerX = state.xRange.mid();
    const auto centerY = state.yRange.mid();
    const auto halfWidth = state.xRange.size() * zoom * 0.5;
    const auto halfHeight = state.yRange.size() * zoom * 0.5;
    return {ViewportChangedEvent{
        Interval{centerX - halfWidth, centerX + halfWidth},
        Interval{centerY - halfHeight, centerY + halfHeight},
        state.framebufferWidth,
        state.framebufferHeight
    }};
}

std::vector<InputEvent> InteractionController::handleResize(const int width,
                                                            const int height,
                                                            const AppState &state) const
{
    if (width <= 0 || height <= 0)
    {
        return {};
    }
    return {ViewportChangedEvent{state.xRange, state.yRange, width, height}};
}

bool InteractionController::printableAscii(const unsigned int codepoint)
{
    return codepoint >= 32 && codepoint < 127;
}
}
