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
                state.formulaInput = {};
                ++state.requestId;
                ++state.generation;
                diff.formulaChanged = true;
                diff.viewportChanged = true;
                diff.renderInvalidated = true;
            }
        }
        else if constexpr (std::is_same_v<Event, BeginFormulaInputEvent>)
        {
            state.formulaInput.active = true;
            state.formulaInput.buffer = state.formulaExpression;
            state.formulaInput.cursor = state.formulaInput.buffer.size();
            state.formulaInput.error.clear();
            diff.renderInvalidated = true;
        }
        else if constexpr (std::is_same_v<Event, AppendFormulaInputEvent>)
        {
            if (state.formulaInput.active && !typedEvent.text.empty())
            {
                state.formulaInput.buffer.insert(state.formulaInput.cursor, typedEvent.text);
                state.formulaInput.cursor += typedEvent.text.size();
                state.formulaInput.error.clear();
                diff.renderInvalidated = true;
            }
        }
        else if constexpr (std::is_same_v<Event, BackspaceFormulaInputEvent>)
        {
            if (state.formulaInput.active && state.formulaInput.cursor > 0)
            {
                state.formulaInput.buffer.erase(state.formulaInput.cursor - 1, 1);
                --state.formulaInput.cursor;
                state.formulaInput.error.clear();
                diff.renderInvalidated = true;
            }
        }
        else if constexpr (std::is_same_v<Event, MoveFormulaCursorEvent>)
        {
            if (state.formulaInput.active)
            {
                const auto previousCursor = state.formulaInput.cursor;
                switch (typedEvent.motion)
                {
                case FormulaCursorMotion::Left:
                    if (state.formulaInput.cursor > 0)
                    {
                        --state.formulaInput.cursor;
                    }
                    break;
                case FormulaCursorMotion::Right:
                    if (state.formulaInput.cursor < state.formulaInput.buffer.size())
                    {
                        ++state.formulaInput.cursor;
                    }
                    break;
                case FormulaCursorMotion::Home:
                    state.formulaInput.cursor = 0;
                    break;
                case FormulaCursorMotion::End:
                    state.formulaInput.cursor = state.formulaInput.buffer.size();
                    break;
                }
                diff.renderInvalidated = previousCursor != state.formulaInput.cursor;
            }
        }
        else if constexpr (std::is_same_v<Event, DeleteFormulaInputEvent>)
        {
            if (state.formulaInput.active && state.formulaInput.cursor < state.formulaInput.buffer.size())
            {
                state.formulaInput.buffer.erase(state.formulaInput.cursor, 1);
                state.formulaInput.error.clear();
                diff.renderInvalidated = true;
            }
        }
        else if constexpr (std::is_same_v<Event, RejectFormulaInputEvent>)
        {
            if (state.formulaInput.active)
            {
                state.formulaInput.error = typedEvent.message;
                diff.renderInvalidated = true;
            }
        }
        else if constexpr (std::is_same_v<Event, CancelFormulaInputEvent>)
        {
            if (state.formulaInput.active)
            {
                state.formulaInput = {};
                diff.renderInvalidated = true;
            }
        }
        else if constexpr (std::is_same_v<Event, SubmitFormulaInputEvent>)
        {
            if (state.formulaInput.active)
            {
                const auto nextExpression = state.formulaInput.buffer;
                state.formulaInput = {};
                diff.renderInvalidated = true;
                if (!nextExpression.empty() && state.formulaExpression != nextExpression)
                {
                    state.formulaExpression = nextExpression;
                    ++state.requestId;
                    ++state.generation;
                    diff.formulaChanged = true;
                    diff.viewportChanged = true;
                }
            }
        }
        else if constexpr (std::is_same_v<Event, RenderTickEvent>)
        {
        }
        else if constexpr (std::is_same_v<Event, ViewportChangedEvent>)
        {
            if (!sameInterval(state.xRange, typedEvent.xRange)
                || !sameInterval(state.yRange, typedEvent.yRange)
                || state.framebufferWidth != typedEvent.framebufferWidth
                || state.framebufferHeight != typedEvent.framebufferHeight
                || state.devicePixelRatio != typedEvent.devicePixelRatio)
            {
                state.xRange = typedEvent.xRange;
                state.yRange = typedEvent.yRange;
                state.framebufferWidth = typedEvent.framebufferWidth;
                state.framebufferHeight = typedEvent.framebufferHeight;
                state.devicePixelRatio = typedEvent.devicePixelRatio;
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
        .invalidateTextLayout = diff.viewportChanged || diff.renderInvalidated
    };
}
}
