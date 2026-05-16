//
// Created by Codex on 3/1/2026.
//

#include "catch.hpp"
#include "../src/Util/InputScenarioRunner.h"

#include <cmath>

TEST_CASE("InputScenarioRunner parses valid script", "[InputScenarioRunner]")
{
    const auto config = InputScenarioRunner::parseScript(
        "drag(2.5,-1.0,3); resize(1280,720,1); scroll(0.5,2); pause(4); key(D,press); capture(D:\\GraphXplorer\\frame.png); formula(x<y); text(+1); click(32,24)");
    REQUIRE(config.has_value());
    REQUIRE(config->actions.size() == 9);

    REQUIRE(config->actions[0].type == InputScenarioRunner::ActionType::Drag);
    REQUIRE(std::abs(config->actions[0].x - 2.5) < 1e-9);
    REQUIRE(std::abs(config->actions[0].y + 1.0) < 1e-9);
    REQUIRE(config->actions[0].frames == 3);

    REQUIRE(config->actions[1].type == InputScenarioRunner::ActionType::Resize);
    REQUIRE(std::abs(config->actions[1].x - 1280.0) < 1e-9);
    REQUIRE(std::abs(config->actions[1].y - 720.0) < 1e-9);
    REQUIRE(config->actions[1].frames == 1);

    REQUIRE(config->actions[2].type == InputScenarioRunner::ActionType::Scroll);
    REQUIRE(std::abs(config->actions[2].value - 0.5) < 1e-9);
    REQUIRE(config->actions[2].frames == 2);

    REQUIRE(config->actions[3].type == InputScenarioRunner::ActionType::Pause);
    REQUIRE(config->actions[3].frames == 4);

    REQUIRE(config->actions[4].type == InputScenarioRunner::ActionType::Key);
    REQUIRE(config->actions[4].text == "D");
    REQUIRE(config->actions[4].state == "press");

    REQUIRE(config->actions[5].type == InputScenarioRunner::ActionType::Capture);
    REQUIRE(config->actions[5].text == "D:\\GraphXplorer\\frame.png");

    REQUIRE(config->actions[6].type == InputScenarioRunner::ActionType::Formula);
    REQUIRE(config->actions[6].text == "x<y");

    REQUIRE(config->actions[7].type == InputScenarioRunner::ActionType::Text);
    REQUIRE(config->actions[7].text == "+1");

    REQUIRE(config->actions[8].type == InputScenarioRunner::ActionType::Click);
    REQUIRE(std::abs(config->actions[8].x - 32.0) < 1e-9);
    REQUIRE(std::abs(config->actions[8].y - 24.0) < 1e-9);
}

TEST_CASE("InputScenarioRunner rejects invalid script", "[InputScenarioRunner]")
{
    REQUIRE_FALSE(InputScenarioRunner::parseScript("drag(1,2)").has_value());
    REQUIRE_FALSE(InputScenarioRunner::parseScript("scroll(1,0)").has_value());
    REQUIRE_FALSE(InputScenarioRunner::parseScript("resize(0,100,1)").has_value());
    REQUIRE_FALSE(InputScenarioRunner::parseScript("key(D,hold)").has_value());
    REQUIRE_FALSE(InputScenarioRunner::parseScript("capture()").has_value());
    REQUIRE_FALSE(InputScenarioRunner::parseScript("formula()").has_value());
    REQUIRE_FALSE(InputScenarioRunner::parseScript("text()").has_value());
    REQUIRE_FALSE(InputScenarioRunner::parseScript("click(1)").has_value());
    REQUIRE_FALSE(InputScenarioRunner::parseScript("unknown(1,2,3)").has_value());
}

TEST_CASE("InputScenarioRunner ticks actions deterministically", "[InputScenarioRunner]")
{
    InputScenarioRunner::Config config;
    config.actions = {
        {InputScenarioRunner::ActionType::Drag, 1.0, -2.0, 0.0, 2},
        {InputScenarioRunner::ActionType::Resize, 1920.0, 1080.0, 0.0, 1},
        {InputScenarioRunner::ActionType::Scroll, 0.0, 0.0, 0.75, 3},
        {InputScenarioRunner::ActionType::Pause, 0.0, 0.0, 0.0, 2}
    };
    config.loop = false;
    config.closeOnComplete = true;

    InputScenarioRunner runner{config};

    int dragCount = 0;
    int scrollCount = 0;
    int resizeCount = 0;
    int keyCount = 0;
    int captureCount = 0;
    int clickCount = 0;
    double totalDragX = 0.0;
    double totalDragY = 0.0;
    double totalScroll = 0.0;
    int lastResizeW = 0;
    int lastResizeH = 0;

    for (int i = 0; i < 8; ++i)
    {
        runner.tick(
            [&](const double dx, const double dy)
            {
                dragCount += 1;
                totalDragX += dx;
                totalDragY += dy;
            },
            [&](const double offset)
            {
                scrollCount += 1;
                totalScroll += offset;
            },
            [&](const int width, const int height)
            {
                resizeCount += 1;
                lastResizeW = width;
                lastResizeH = height;
            },
            [&](const std::string &, const std::string &)
            {
                keyCount += 1;
            },
            [&](const std::string &)
            {
                captureCount += 1;
            },
            [](const std::string &) {},
            [](const std::string &) {},
            [&](double, double)
            {
                clickCount += 1;
            });
    }

    REQUIRE(dragCount == 2);
    REQUIRE(resizeCount == 1);
    REQUIRE(lastResizeW == 1920);
    REQUIRE(lastResizeH == 1080);
    REQUIRE(scrollCount == 3);
    REQUIRE(keyCount == 0);
    REQUIRE(captureCount == 0);
    REQUIRE(clickCount == 0);
    REQUIRE(std::abs(totalDragX - 2.0) < 1e-9);
    REQUIRE(std::abs(totalDragY + 4.0) < 1e-9);
    REQUIRE(std::abs(totalScroll - 2.25) < 1e-9);
    REQUIRE(runner.isComplete());
    REQUIRE(runner.shouldCloseOnComplete());
    REQUIRE_FALSE(runner.isActive());
}

TEST_CASE("InputScenarioRunner ticks key and capture actions", "[InputScenarioRunner]")
{
    const auto config = InputScenarioRunner::parseScript(
        "key(D,press);formula(x^2+y^2<25);text(+1);click(44,22);capture(D:\\GraphXplorer\\debug.png)");
    REQUIRE(config.has_value());
    InputScenarioRunner runner{*config};

    std::string keyName;
    std::string keyState;
    std::string capturePath;
    std::string formulaExpression;
    std::string textInput;
    double clickX = 0.0;
    double clickY = 0.0;

    for (int i = 0; i < 5; ++i)
    {
        runner.tick(
            [](double, double) {},
            [](double) {},
            [](int, int) {},
            [&](const std::string &key, const std::string &state)
            {
                keyName = key;
                keyState = state;
            },
            [&](const std::string &path)
            {
                capturePath = path;
            },
            [&](const std::string &expression)
            {
                formulaExpression = expression;
            },
            [&](const std::string &text)
            {
                textInput = text;
            },
            [&](const double x, const double y)
            {
                clickX = x;
                clickY = y;
            });
    }

    REQUIRE(keyName == "D");
    REQUIRE(keyState == "press");
    REQUIRE(formulaExpression == "x^2+y^2<25");
    REQUIRE(textInput == "+1");
    REQUIRE(std::abs(clickX - 44.0) < 1e-9);
    REQUIRE(std::abs(clickY - 22.0) < 1e-9);
    REQUIRE(capturePath == "D:\\GraphXplorer\\debug.png");
    REQUIRE(runner.isComplete());
}
