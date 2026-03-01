//
// Created by Codex on 3/1/2026.
//

#include "InputScenarioRunner.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <ranges>

namespace
{
constexpr char ScriptEnvName[] = "GRAPHX_INPUT_SCRIPT";
constexpr char ScriptLoopEnvName[] = "GRAPHX_INPUT_LOOP";
constexpr char ScriptCloseOnCompleteEnvName[] = "GRAPHX_INPUT_EXIT_ON_COMPLETE";
constexpr char ScriptWaitMsEnvName[] = "GRAPHX_INPUT_WAIT_MS";
}

std::string InputScenarioRunner::trim(const std::string &value)
{
    auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return {};
    }

    auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::vector<std::string> InputScenarioRunner::splitAndTrim(const std::string &input, const char delimiter)
{
    std::vector<std::string> tokens;
    std::string current;
    for (const auto c : input)
    {
        if (c == delimiter)
        {
            auto token = trim(current);
            if (!token.empty())
            {
                tokens.push_back(std::move(token));
            }
            current.clear();
            continue;
        }
        current.push_back(c);
    }

    auto tail = trim(current);
    if (!tail.empty())
    {
        tokens.push_back(std::move(tail));
    }
    return tokens;
}

std::optional<InputScenarioRunner::Action> InputScenarioRunner::parseAction(const std::string &token)
{
    const auto openParenPos = token.find('(');
    const auto closeParenPos = token.rfind(')');
    if (openParenPos == std::string::npos || closeParenPos == std::string::npos || closeParenPos <= openParenPos)
    {
        return std::nullopt;
    }

    const auto name = trim(token.substr(0, openParenPos));
    auto argsString = token.substr(openParenPos + 1, closeParenPos - openParenPos - 1);
    const auto args = splitAndTrim(argsString, ',');

    try
    {
        if (name == "drag" && args.size() == 3)
        {
            Action action;
            action.type = ActionType::Drag;
            action.x = std::stod(args[0]);
            action.y = std::stod(args[1]);
            action.frames = std::stoi(args[2]);
            if (action.frames <= 0)
            {
                return std::nullopt;
            }
            return action;
        }

        if (name == "scroll" && args.size() == 2)
        {
            Action action;
            action.type = ActionType::Scroll;
            action.value = std::stod(args[0]);
            action.frames = std::stoi(args[1]);
            if (action.frames <= 0)
            {
                return std::nullopt;
            }
            return action;
        }

        if (name == "resize" && args.size() == 3)
        {
            Action action;
            action.type = ActionType::Resize;
            action.x = std::stod(args[0]);
            action.y = std::stod(args[1]);
            action.frames = std::stoi(args[2]);
            if (action.frames <= 0 || action.x <= 0.0 || action.y <= 0.0)
            {
                return std::nullopt;
            }
            return action;
        }

        if (name == "pause" && args.size() == 1)
        {
            Action action;
            action.type = ActionType::Pause;
            action.frames = std::stoi(args[0]);
            if (action.frames <= 0)
            {
                return std::nullopt;
            }
            return action;
        }
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }

    return std::nullopt;
}

bool InputScenarioRunner::parseBooleanEnv(const char *value, const bool fallback)
{
    if (!value)
    {
        return fallback;
    }

    auto normalized = std::string(value);
    std::ranges::transform(normalized, normalized.begin(), [](const unsigned char ch)
    {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on")
    {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off")
    {
        return false;
    }
    return fallback;
}

double InputScenarioRunner::parseDoubleEnv(const char *value, const double fallback)
{
    if (!value)
    {
        return fallback;
    }

    try
    {
        return std::stod(value);
    }
    catch (const std::exception &)
    {
        return fallback;
    }
}

std::optional<InputScenarioRunner::Config> InputScenarioRunner::parseScript(const std::string &script)
{
    Config config;
    for (const auto &token : splitAndTrim(script, ';'))
    {
        auto action = parseAction(token);
        if (!action)
        {
            return std::nullopt;
        }
        config.actions.push_back(*action);
    }

    if (config.actions.empty())
    {
        return std::nullopt;
    }
    return config;
}

std::optional<InputScenarioRunner> InputScenarioRunner::fromEnvironment()
{
    const auto *scriptEnv = std::getenv(ScriptEnvName);
    if (!scriptEnv)
    {
        return std::nullopt;
    }

    const auto script = trim(scriptEnv);
    if (script.empty())
    {
        return std::nullopt;
    }

    auto parsedConfig = parseScript(script);
    if (!parsedConfig)
    {
        std::cerr << "[InputScenarioRunner] Failed to parse GRAPHX_INPUT_SCRIPT: " << script << "\n";
        return std::nullopt;
    }

    parsedConfig->loop = parseBooleanEnv(std::getenv(ScriptLoopEnvName), false);
    parsedConfig->closeOnComplete = parseBooleanEnv(std::getenv(ScriptCloseOnCompleteEnvName), false);
    parsedConfig->waitTimeoutSeconds = std::max(0.0, parseDoubleEnv(std::getenv(ScriptWaitMsEnvName), 8.0)) / 1000.0;

    return InputScenarioRunner{std::move(*parsedConfig)};
}

InputScenarioRunner::InputScenarioRunner(Config config): config(std::move(config))
{
}

bool InputScenarioRunner::isActive() const
{
    return !complete && !config.actions.empty();
}

bool InputScenarioRunner::isComplete() const
{
    return complete;
}

bool InputScenarioRunner::shouldCloseOnComplete() const
{
    return complete && config.closeOnComplete;
}

double InputScenarioRunner::waitTimeoutSeconds() const
{
    return config.waitTimeoutSeconds;
}

void InputScenarioRunner::tick(const DragCallback &onDrag,
                               const ScrollCallback &onScroll,
                               const ResizeCallback &onResize)
{
    if (!isActive())
    {
        return;
    }

    const auto &action = config.actions[actionIndex];
    if (action.type == ActionType::Drag)
    {
        onDrag(action.x, action.y);
    }
    else if (action.type == ActionType::Scroll)
    {
        onScroll(action.value);
    }
    else if (action.type == ActionType::Resize)
    {
        onResize(static_cast<int>(action.x), static_cast<int>(action.y));
    }

    frameInAction += 1;
    if (frameInAction < action.frames)
    {
        return;
    }

    frameInAction = 0;
    actionIndex += 1;
    if (actionIndex < config.actions.size())
    {
        return;
    }

    if (config.loop)
    {
        actionIndex = 0;
        return;
    }

    complete = true;
}
