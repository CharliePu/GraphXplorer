#include "AppState.h"

#include <sstream>
#include <type_traits>

namespace gx
{
StateDiff AppStateReducer::reduce(AppState &state, const InputEvent &event) const
{
    StateDiff diff;

    std::visit([&](const auto &typedEvent)
    {
        using Event = std::decay_t<decltype(typedEvent)>;
        if constexpr (std::is_same_v<Event, FormulaInputEvent>)
        {
            if (state.formulaExpression != typedEvent.expression)
            {
                state.formulaExpression = typedEvent.expression;
                ++state.requestId;
                ++state.generation;
                diff.formulaChanged = true;
                diff.viewportChanged = true;
                diff.renderInvalidated = true;
            }
        }
        else if constexpr (std::is_same_v<Event, ViewportChangedEvent>)
        {
            if (!sameInterval(state.xRange, typedEvent.xRange)
                || !sameInterval(state.yRange, typedEvent.yRange)
                || state.framebufferWidth != typedEvent.framebufferWidth
                || state.framebufferHeight != typedEvent.framebufferHeight)
            {
                state.xRange = typedEvent.xRange;
                state.yRange = typedEvent.yRange;
                state.framebufferWidth = typedEvent.framebufferWidth;
                state.framebufferHeight = typedEvent.framebufferHeight;
                ++state.requestId;
                diff.viewportChanged = true;
                diff.renderInvalidated = true;
            }
        }
        else if constexpr (std::is_same_v<Event, DebugToggleEvent>)
        {
            if (state.debug != typedEvent.enabled)
            {
                state.debug = typedEvent.enabled;
                diff.renderInvalidated = true;
            }
        }
    }, event);

    diff.requestId = state.requestId;
    diff.generation = state.generation;

    std::ostringstream out;
    out << "formulaChanged=" << diff.formulaChanged
        << ",viewportChanged=" << diff.viewportChanged
        << ",renderInvalidated=" << diff.renderInvalidated
        << ",requestId=" << diff.requestId
        << ",generation=" << diff.generation;
    diff.debugString = out.str();

    return diff;
}

EffectPlan EffectPlanner::plan(const StateDiff &diff) const
{
    return {
        .compileFormula = diff.formulaChanged,
        .requestTiles = diff.formulaChanged || diff.viewportChanged,
        .invalidateRender = diff.renderInvalidated,
        .invalidateTextLayout = diff.viewportChanged
    };
}
}
