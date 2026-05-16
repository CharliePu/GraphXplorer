#include "InteractionController.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "UiLayout.h"

namespace gx
{
namespace
{
Interval rangeAround(const double center, const double span)
{
    const auto halfSpan = span * 0.5;
    return {center - halfSpan, center + halfSpan};
}

ViewportChangedEvent viewportForScale(const double centerX,
                                      const double centerY,
                                      const double worldUnitsPerPixel,
                                      const int width,
                                      const int height,
                                      const double devicePixelRatio)
{
    const auto xSpan = worldUnitsPerPixel * static_cast<double>(width);
    const auto ySpan = worldUnitsPerPixel * static_cast<double>(height);
    return {
        rangeAround(centerX, xSpan),
        rangeAround(centerY, ySpan),
        width,
        height,
        devicePixelRatio
    };
}
}

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
        else if (key == glfw::KeyCode::Delete && action != glfw::KeyState::Release)
        {
            events.emplace_back(DeleteFormulaInputEvent{});
        }
        else if (key == glfw::KeyCode::Left && action != glfw::KeyState::Release)
        {
            events.emplace_back(MoveFormulaCursorEvent{FormulaCursorMotion::Left});
        }
        else if (key == glfw::KeyCode::Right && action != glfw::KeyState::Release)
        {
            events.emplace_back(MoveFormulaCursorEvent{FormulaCursorMotion::Right});
        }
        else if (key == glfw::KeyCode::Home && action != glfw::KeyState::Release)
        {
            events.emplace_back(MoveFormulaCursorEvent{FormulaCursorMotion::Home});
        }
        else if (key == glfw::KeyCode::End && action != glfw::KeyState::Release)
        {
            events.emplace_back(MoveFormulaCursorEvent{FormulaCursorMotion::End});
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
        events.emplace_back(resetViewportEvent(state));
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
        state.framebufferHeight,
        state.devicePixelRatio
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
        state.framebufferHeight,
        state.devicePixelRatio
    }};
}

std::vector<InputEvent> InteractionController::handleClick(const double x,
                                                           const double y,
                                                           const AppState &state) const
{
    const auto action = hitTestUiAction(x, y, state);
    if (!action)
    {
        return {};
    }

    switch (*action)
    {
    case UiAction::BeginFormulaInput:
        return {BeginFormulaInputEvent{}};
    case UiAction::SubmitFormulaInput:
        return {SubmitFormulaInputEvent{}};
    case UiAction::CancelFormulaInput:
        return {CancelFormulaInputEvent{}};
    case UiAction::ResetViewport:
        return {resetViewportEvent(state)};
    case UiAction::ToggleDebug:
        return {DebugToggleEvent{!state.debug}};
    }

    return {};
}

std::vector<InputEvent> InteractionController::handleResize(const int width,
                                                            const int height,
                                                            const AppState &state,
                                                            const double devicePixelRatio) const
{
    if (width <= 0 || height <= 0)
    {
        return {};
    }
    return {resizeViewportEvent(width, height, state, devicePixelRatio)};
}

ViewportChangedEvent InteractionController::resizeViewportEvent(const int width,
                                                                const int height,
                                                                const AppState &state,
                                                                const double devicePixelRatio)
{
    const auto oldWidth = std::max(1, state.framebufferWidth);
    const auto oldHeight = std::max(1, state.framebufferHeight);
    const auto xUnitsPerPixel = state.xRange.size() / static_cast<double>(oldWidth);
    const auto yUnitsPerPixel = state.yRange.size() / static_cast<double>(oldHeight);
    const auto worldUnitsPerPixel = std::max(xUnitsPerPixel, yUnitsPerPixel);
    return viewportForScale(state.xRange.mid(), state.yRange.mid(), worldUnitsPerPixel, width, height,
                            devicePixelRatio);
}

ViewportChangedEvent InteractionController::resetViewportEvent(const AppState &state)
{
    constexpr auto baseSpan = 40.0;
    const auto width = std::max(1, state.framebufferWidth);
    const auto height = std::max(1, state.framebufferHeight);
    const auto worldUnitsPerPixel = baseSpan / static_cast<double>(std::min(width, height));
    return viewportForScale(0.0, 0.0, worldUnitsPerPixel, width, height, state.devicePixelRatio);
}

bool InteractionController::printableAscii(const unsigned int codepoint)
{
    return codepoint >= 32 && codepoint < 127;
}
}
