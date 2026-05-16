#ifndef APPSTATE_H
#define APPSTATE_H

#include <optional>
#include <string>
#include <variant>

#include "../Math/Interval.h"

namespace gx
{
struct FormulaInputEvent
{
    std::string expression{};
};

struct BeginFormulaInputEvent
{
};

struct AppendFormulaInputEvent
{
    std::string text{};
};

struct BackspaceFormulaInputEvent
{
};

struct CancelFormulaInputEvent
{
};

struct SubmitFormulaInputEvent
{
};

struct RenderTickEvent
{
};

struct ViewportChangedEvent
{
    Interval xRange{};
    Interval yRange{};
    int framebufferWidth{0};
    int framebufferHeight{0};
};

struct DebugToggleEvent
{
    bool enabled{false};
};

using InputEvent = std::variant<
    FormulaInputEvent,
    BeginFormulaInputEvent,
    AppendFormulaInputEvent,
    BackspaceFormulaInputEvent,
    CancelFormulaInputEvent,
    SubmitFormulaInputEvent,
    RenderTickEvent,
    ViewportChangedEvent,
    DebugToggleEvent>;

struct FormulaInputState
{
    bool active{false};
    std::string buffer{};
    size_t cursor{0};
    bool operator==(const FormulaInputState &) const = default;
};

struct AppState
{
    std::string formulaExpression{"x<=y"};
    FormulaInputState formulaInput{};
    Interval xRange{-20.0, 20.0};
    Interval yRange{-20.0, 20.0};
    int framebufferWidth{800};
    int framebufferHeight{800};
    bool debug{false};
    uint64_t requestId{1};
    uint64_t generation{1};
};

struct StateDiff
{
    bool formulaChanged{false};
    bool viewportChanged{false};
    bool renderInvalidated{false};
    uint64_t requestId{0};
    uint64_t generation{0};
    std::string debugString{};
};

class AppStateReducer
{
public:
    [[nodiscard]] StateDiff reduce(AppState &state, const InputEvent &event) const;
};

struct EffectPlan
{
    bool compileFormula{false};
    bool requestTiles{false};
    bool invalidateRender{false};
    bool invalidateTextLayout{false};
};

class EffectPlanner
{
public:
    [[nodiscard]] EffectPlan plan(const StateDiff &diff) const;
};
}

#endif // APPSTATE_H
