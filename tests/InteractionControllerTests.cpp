#include "catch.hpp"

#include <cmath>
#include <variant>

#include "../src/App/InteractionController.h"
#include "../src/App/UiLayout.h"

namespace
{
template <typename Event>
Event singleEvent(const std::vector<gx::InputEvent> &events)
{
    REQUIRE(events.size() == 1);
    const auto *event = std::get_if<Event>(&events.front());
    REQUIRE(event != nullptr);
    return *event;
}

gx::ViewportChangedEvent singleViewportEvent(const std::vector<gx::InputEvent> &events)
{
    return singleEvent<gx::ViewportChangedEvent>(events);
}

void requireClose(const double actual, const double expected)
{
    CHECK(std::abs(actual - expected) < 1e-9);
}
}

TEST_CASE("InteractionController resize expands world range to match framebuffer aspect", "[InteractionController]")
{
    gx::AppState state;
    state.xRange = {-10.0, 10.0};
    state.yRange = {-10.0, 10.0};
    state.framebufferWidth = 100;
    state.framebufferHeight = 100;

    const auto event = singleViewportEvent(gx::InteractionController{}.handleResize(200, 100, state));

    requireClose(event.xRange.lower, -20.0);
    requireClose(event.xRange.upper, 20.0);
    requireClose(event.yRange.lower, -10.0);
    requireClose(event.yRange.upper, 10.0);
    CHECK(event.framebufferWidth == 200);
    CHECK(event.framebufferHeight == 100);
}

TEST_CASE("InteractionController resize preserves scale and center reversibly", "[InteractionController]")
{
    gx::AppState state;
    state.xRange = {-20.0, 20.0};
    state.yRange = {-10.0, 10.0};
    state.framebufferWidth = 200;
    state.framebufferHeight = 100;

    const auto event = singleViewportEvent(gx::InteractionController{}.handleResize(100, 100, state));

    requireClose(event.xRange.lower, -10.0);
    requireClose(event.xRange.upper, 10.0);
    requireClose(event.yRange.lower, -10.0);
    requireClose(event.yRange.upper, 10.0);
}

TEST_CASE("InteractionController resize preserves panned center", "[InteractionController]")
{
    gx::AppState state;
    state.xRange = {5.0, 15.0};
    state.yRange = {-3.0, 7.0};
    state.framebufferWidth = 100;
    state.framebufferHeight = 100;

    const auto event = singleViewportEvent(gx::InteractionController{}.handleResize(200, 100, state));

    requireClose(event.xRange.lower, 0.0);
    requireClose(event.xRange.upper, 20.0);
    requireClose(event.yRange.lower, -3.0);
    requireClose(event.yRange.upper, 7.0);
}

TEST_CASE("InteractionController home reset respects current framebuffer aspect", "[InteractionController]")
{
    gx::AppState state;
    state.framebufferWidth = 200;
    state.framebufferHeight = 100;

    const auto event = singleViewportEvent(gx::InteractionController{}.handleKey(
        glfw::KeyCode::H,
        glfw::KeyState::Press,
        state));

    requireClose(event.xRange.lower, -40.0);
    requireClose(event.xRange.upper, 40.0);
    requireClose(event.yRange.lower, -20.0);
    requireClose(event.yRange.upper, 20.0);
    CHECK(event.framebufferWidth == 200);
    CHECK(event.framebufferHeight == 100);
}

TEST_CASE("InteractionController consumes top-level keyboard shortcuts", "[InteractionController]")
{
    gx::AppState state;

    const auto begin = singleEvent<gx::BeginFormulaInputEvent>(gx::InteractionController{}.handleKey(
        glfw::KeyCode::I,
        glfw::KeyState::Release,
        state));
    (void)begin;

    const auto debug = singleEvent<gx::DebugToggleEvent>(gx::InteractionController{}.handleKey(
        glfw::KeyCode::D,
        glfw::KeyState::Press,
        state));
    CHECK(debug.enabled);

    CHECK(gx::InteractionController{}.handleKey(glfw::KeyCode::I, glfw::KeyState::Press, state).empty());
    CHECK(gx::InteractionController{}.handleKey(glfw::KeyCode::Escape, glfw::KeyState::Press, state).empty());
}

TEST_CASE("InteractionController routes editing keys only while formula input is active", "[InteractionController]")
{
    gx::AppState state;
    state.formulaInput.active = true;

    const auto submit = singleEvent<gx::SubmitFormulaInputEvent>(gx::InteractionController{}.handleKey(
        glfw::KeyCode::Enter,
        glfw::KeyState::Press,
        state));
    (void)submit;

    const auto backspace = singleEvent<gx::BackspaceFormulaInputEvent>(gx::InteractionController{}.handleKey(
        glfw::KeyCode::Backspace,
        glfw::KeyState::Repeat,
        state));
    (void)backspace;

    const auto deleteEvent = singleEvent<gx::DeleteFormulaInputEvent>(gx::InteractionController{}.handleKey(
        glfw::KeyCode::Delete,
        glfw::KeyState::Press,
        state));
    (void)deleteEvent;

    const auto cursorLeft = singleEvent<gx::MoveFormulaCursorEvent>(gx::InteractionController{}.handleKey(
        glfw::KeyCode::Left,
        glfw::KeyState::Repeat,
        state));
    CHECK(cursorLeft.motion == gx::FormulaCursorMotion::Left);

    const auto cursorRight = singleEvent<gx::MoveFormulaCursorEvent>(gx::InteractionController{}.handleKey(
        glfw::KeyCode::Right,
        glfw::KeyState::Press,
        state));
    CHECK(cursorRight.motion == gx::FormulaCursorMotion::Right);

    const auto cursorHome = singleEvent<gx::MoveFormulaCursorEvent>(gx::InteractionController{}.handleKey(
        glfw::KeyCode::Home,
        glfw::KeyState::Press,
        state));
    CHECK(cursorHome.motion == gx::FormulaCursorMotion::Home);

    const auto cursorEnd = singleEvent<gx::MoveFormulaCursorEvent>(gx::InteractionController{}.handleKey(
        glfw::KeyCode::End,
        glfw::KeyState::Press,
        state));
    CHECK(cursorEnd.motion == gx::FormulaCursorMotion::End);

    const auto cancel = singleEvent<gx::CancelFormulaInputEvent>(gx::InteractionController{}.handleKey(
        glfw::KeyCode::Escape,
        glfw::KeyState::Press,
        state));
    (void)cancel;

    CHECK(gx::InteractionController{}.handleKey(glfw::KeyCode::D, glfw::KeyState::Press, state).empty());
    CHECK(gx::InteractionController{}.handleKey(glfw::KeyCode::H, glfw::KeyState::Press, state).empty());
    CHECK(gx::InteractionController{}.handleKey(glfw::KeyCode::Escape, glfw::KeyState::Release, state).empty());
}

TEST_CASE("InteractionController routes chrome clicks through shared UI hit testing", "[InteractionController]")
{
    gx::AppState state;
    state.framebufferWidth = 800;
    state.framebufferHeight = 600;

    const auto statusLayout = gx::statusOverlayLayoutFor(state);
    REQUIRE(statusLayout.buttons.size() == 3);
    const auto editButton = statusLayout.buttons.front().bounds;
    const auto begin = singleEvent<gx::BeginFormulaInputEvent>(gx::InteractionController{}.handleClick(
        (editButton.left + editButton.right) * 0.5,
        (editButton.top + editButton.bottom) * 0.5,
        state));
    (void)begin;

    const auto debugButton = statusLayout.buttons.back().bounds;
    const auto debug = singleEvent<gx::DebugToggleEvent>(gx::InteractionController{}.handleClick(
        (debugButton.left + debugButton.right) * 0.5,
        (debugButton.top + debugButton.bottom) * 0.5,
        state));
    CHECK(debug.enabled);

    state.formulaInput.active = true;
    state.formulaInput.buffer = "x<=y";
    state.formulaInput.cursor = state.formulaInput.buffer.size();
    const auto formulaLayout = gx::formulaOverlayLayoutFor(state);
    REQUIRE(formulaLayout.buttons.size() == 2);
    const auto applyButton = formulaLayout.buttons.front().bounds;
    const auto submit = singleEvent<gx::SubmitFormulaInputEvent>(gx::InteractionController{}.handleClick(
        (applyButton.left + applyButton.right) * 0.5,
        (applyButton.top + applyButton.bottom) * 0.5,
        state));
    (void)submit;

    const auto cancelButton = formulaLayout.buttons.back().bounds;
    const auto cancel = singleEvent<gx::CancelFormulaInputEvent>(gx::InteractionController{}.handleClick(
        (cancelButton.left + cancelButton.right) * 0.5,
        (cancelButton.top + cancelButton.bottom) * 0.5,
        state));
    (void)cancel;
}

TEST_CASE("AppStateReducer edits formula input around the cursor", "[InteractionController]")
{
    gx::AppState state;
    state.formulaInput.active = true;
    state.formulaInput.buffer = "x<=y";
    state.formulaInput.cursor = 2;
    const gx::AppStateReducer reducer;

    [[maybe_unused]] auto diff = reducer.reduce(state, gx::MoveFormulaCursorEvent{gx::FormulaCursorMotion::Left});
    CHECK(state.formulaInput.cursor == 1);

    diff = reducer.reduce(state, gx::DeleteFormulaInputEvent{});
    CHECK(state.formulaInput.buffer == "x=y");
    CHECK(state.formulaInput.cursor == 1);

    diff = reducer.reduce(state, gx::AppendFormulaInputEvent{"<"});
    CHECK(state.formulaInput.buffer == "x<=y");
    CHECK(state.formulaInput.cursor == 2);

    diff = reducer.reduce(state, gx::MoveFormulaCursorEvent{gx::FormulaCursorMotion::Home});
    CHECK(state.formulaInput.cursor == 0);

    diff = reducer.reduce(state, gx::BackspaceFormulaInputEvent{});
    CHECK(state.formulaInput.buffer == "x<=y");
    CHECK(state.formulaInput.cursor == 0);

    diff = reducer.reduce(state, gx::MoveFormulaCursorEvent{gx::FormulaCursorMotion::End});
    CHECK(state.formulaInput.cursor == state.formulaInput.buffer.size());
}

TEST_CASE("InteractionController accepts printable text only during formula input", "[InteractionController]")
{
    gx::AppState state;

    CHECK(gx::InteractionController{}.handleText('x', state).empty());

    state.formulaInput.active = true;
    const auto append = singleEvent<gx::AppendFormulaInputEvent>(gx::InteractionController{}.handleText('x', state));
    CHECK(append.text == "x");
    CHECK(gx::InteractionController{}.handleText('\n', state).empty());
    CHECK(gx::InteractionController{}.handleText(127, state).empty());
}

TEST_CASE("InteractionController pans and zooms only outside formula input", "[InteractionController]")
{
    gx::AppState state;
    state.xRange = {-20.0, 20.0};
    state.yRange = {-10.0, 10.0};
    state.framebufferWidth = 400;
    state.framebufferHeight = 200;

    const auto drag = singleViewportEvent(gx::InteractionController{}.handleDrag(40.0, -20.0, state));
    requireClose(drag.xRange.lower, -24.0);
    requireClose(drag.xRange.upper, 16.0);
    requireClose(drag.yRange.lower, -12.0);
    requireClose(drag.yRange.upper, 8.0);

    const auto scroll = singleViewportEvent(gx::InteractionController{}.handleScroll(1.0, state));
    requireClose(scroll.xRange.lower, -18.0);
    requireClose(scroll.xRange.upper, 18.0);
    requireClose(scroll.yRange.lower, -9.0);
    requireClose(scroll.yRange.upper, 9.0);

    state.formulaInput.active = true;
    CHECK(gx::InteractionController{}.handleDrag(40.0, -20.0, state).empty());
    CHECK(gx::InteractionController{}.handleScroll(1.0, state).empty());
}
