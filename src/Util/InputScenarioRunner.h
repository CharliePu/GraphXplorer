//
// Created by Codex on 3/1/2026.
//

#ifndef INPUTSCENARIORUNNER_H
#define INPUTSCENARIORUNNER_H

#include <functional>
#include <optional>
#include <string>
#include <vector>

class InputScenarioRunner
{
public:
    enum class ActionType
    {
        Drag,
        Scroll,
        Resize,
        Pause,
        Key,
        Capture,
        Formula,
        Text,
        Click,
        Close
    };

    struct Action
    {
        ActionType type{ActionType::Pause};
        double x{0.0};
        double y{0.0};
        double value{0.0};
        int frames{1};
        std::string text{};
        std::string state{};
    };

    struct Config
    {
        std::vector<Action> actions{};
        bool loop{false};
        bool closeOnComplete{false};
        double waitTimeoutSeconds{1.0 / 120.0};
    };

    using DragCallback = std::function<void(double, double)>;
    using ScrollCallback = std::function<void(double)>;
    using ResizeCallback = std::function<void(int, int)>;
    using KeyCallback = std::function<void(const std::string &, const std::string &)>;
    using CaptureCallback = std::function<void(const std::string &)>;
    using FormulaCallback = std::function<void(const std::string &)>;
    using TextCallback = std::function<void(const std::string &)>;
    using ClickCallback = std::function<void(double, double)>;
    using CloseCallback = std::function<void()>;

    static std::optional<Config> parseScript(const std::string &script);
    static std::optional<InputScenarioRunner> fromEnvironment();

    explicit InputScenarioRunner(Config config);

    bool isActive() const;
    bool isComplete() const;
    bool shouldCloseOnComplete() const;
    double waitTimeoutSeconds() const;

    void tick(const DragCallback &onDrag, const ScrollCallback &onScroll, const ResizeCallback &onResize);
    void tick(const DragCallback &onDrag,
              const ScrollCallback &onScroll,
              const ResizeCallback &onResize,
              const KeyCallback &onKey,
              const CaptureCallback &onCapture,
              const FormulaCallback &onFormula = [](const std::string &) {},
              const TextCallback &onText = [](const std::string &) {},
              const ClickCallback &onClick = [](double, double) {},
              const CloseCallback &onClose = [] {});

private:
    static std::string trim(const std::string &value);
    static std::vector<std::string> splitAndTrim(const std::string &input, char delimiter);
    static std::optional<Action> parseAction(const std::string &token);
    static bool parseBooleanEnv(const char *value, bool fallback);
    static double parseDoubleEnv(const char *value, double fallback);

    Config config;
    size_t actionIndex{0};
    int frameInAction{0};
    bool complete{false};
};

#endif // INPUTSCENARIORUNNER_H
