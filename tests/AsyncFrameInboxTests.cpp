//
// Created by Codex on 3/1/2026.
//

#include "catch.hpp"
#include "../src/Util/AsyncFrameInbox.h"

TEST_CASE("AsyncFrameInbox HandleN drains limited items per frame", "[AsyncFrameInbox]")
{
    AsyncFrameInbox<int> inbox{{AsyncFrameInbox<int>::Mode::HandleN, 2}};
    inbox.push(1);
    inbox.push(2);
    inbox.push(3);

    const auto firstFrame = inbox.drainForFrame();
    REQUIRE(firstFrame == std::vector<int>{1, 2});
    REQUIRE(inbox.pendingCount() == 1);

    const auto secondFrame = inbox.drainForFrame();
    REQUIRE(secondFrame == std::vector<int>{3});
    REQUIRE(inbox.empty());
}

TEST_CASE("AsyncFrameInbox LatestOnly keeps only latest item", "[AsyncFrameInbox]")
{
    AsyncFrameInbox<int> inbox{{AsyncFrameInbox<int>::Mode::LatestOnly, 8}};
    inbox.push(10);
    inbox.push(20);
    inbox.push(30);

    const auto frame = inbox.drainForFrame();
    REQUIRE(frame == std::vector<int>{30});
    REQUIRE(inbox.empty());
}

TEST_CASE("AsyncFrameInbox HandleAll drains all queued items", "[AsyncFrameInbox]")
{
    AsyncFrameInbox<int> inbox{{AsyncFrameInbox<int>::Mode::HandleAll, 8}};
    inbox.push(4);
    inbox.push(5);
    inbox.push(6);

    const auto frame = inbox.drainForFrame();
    REQUIRE(frame == std::vector<int>{4, 5, 6});
    REQUIRE(inbox.empty());
}

