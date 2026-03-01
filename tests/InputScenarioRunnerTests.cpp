//
// Created by Codex on 3/1/2026.
//

#include "catch.hpp"
#include "../src/Util/InputScenarioRunner.h"

#include <cmath>

TEST_CASE("InputScenarioRunner parses valid script", "[InputScenarioRunner]")
{
    const auto config = InputScenarioRunner::parseScript("drag(2.5,-1.0,3); resize(1280,720,1); scroll(0.5,2); pause(4)");
    REQUIRE(config.has_value());
    REQUIRE(config->actions.size() == 4);

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
}

TEST_CASE("InputScenarioRunner rejects invalid script", "[InputScenarioRunner]")
{
    REQUIRE_FALSE(InputScenarioRunner::parseScript("drag(1,2)").has_value());
    REQUIRE_FALSE(InputScenarioRunner::parseScript("scroll(1,0)").has_value());
    REQUIRE_FALSE(InputScenarioRunner::parseScript("resize(0,100,1)").has_value());
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
            });
    }

    REQUIRE(dragCount == 2);
    REQUIRE(resizeCount == 1);
    REQUIRE(lastResizeW == 1920);
    REQUIRE(lastResizeH == 1080);
    REQUIRE(scrollCount == 3);
    REQUIRE(std::abs(totalDragX - 2.0) < 1e-9);
    REQUIRE(std::abs(totalDragY + 4.0) < 1e-9);
    REQUIRE(std::abs(totalScroll - 2.25) < 1e-9);
    REQUIRE(runner.isComplete());
    REQUIRE(runner.shouldCloseOnComplete());
    REQUIRE_FALSE(runner.isActive());
}
