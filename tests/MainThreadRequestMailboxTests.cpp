#include "catch.hpp"

#include <filesystem>
#include <variant>

#include "../src/Core/MainThreadRequestMailbox.h"

namespace
{
gx::ViewportChangedEvent viewportEvent(const int width, const int height)
{
    return gx::ViewportChangedEvent{
        .xRange = Interval{-1.0, 1.0},
        .yRange = Interval{-1.0, 1.0},
        .framebufferWidth = width,
        .framebufferHeight = height,
        .devicePixelRatio = 1.0
    };
}
}

TEST_CASE("MainThreadRequestMailbox coalesces resize requests and wake posts", "[MainThreadRequestMailbox]")
{
    auto wakePosts = 0;
    gx::MainThreadRequestMailbox mailbox{[&] { ++wakePosts; }};

    CHECK(mailbox.submitResize(800, 600));
    CHECK_FALSE(mailbox.submitResize(1024, 768));
    CHECK_FALSE(mailbox.submitFrameWake("compute"));

    auto batch = mailbox.drain();
    REQUIRE(batch.resize.has_value());
    CHECK(batch.resize->width == 1024);
    CHECK(batch.resize->height == 768);
    CHECK(batch.frameWake);
    CHECK(batch.coalescedResizeRequests == 1);
    CHECK(wakePosts == 1);
    CHECK_FALSE(mailbox.hasPendingWork());
}

TEST_CASE("MainThreadRequestMailbox keeps ordered input but coalesces viewport tail",
          "[MainThreadRequestMailbox]")
{
    auto wakePosts = 0;
    gx::MainThreadRequestMailbox mailbox{[&] { ++wakePosts; }};

    CHECK(mailbox.submitInput(gx::BeginFormulaInputEvent{}));
    CHECK_FALSE(mailbox.submitInput(viewportEvent(400, 400)));
    CHECK_FALSE(mailbox.submitInput(viewportEvent(900, 700)));

    const auto batch = mailbox.drain();
    REQUIRE(batch.inputEvents.size() == 2);
    CHECK(std::holds_alternative<gx::BeginFormulaInputEvent>(batch.inputEvents[0]));
    REQUIRE(std::holds_alternative<gx::ViewportChangedEvent>(batch.inputEvents[1]));
    const auto &viewport = std::get<gx::ViewportChangedEvent>(batch.inputEvents[1]);
    CHECK(viewport.framebufferWidth == 900);
    CHECK(viewport.framebufferHeight == 700);
    CHECK(batch.coalescedViewportEvents == 1);
    CHECK(wakePosts == 1);
}

TEST_CASE("MainThreadRequestMailbox drain rearms wake posting", "[MainThreadRequestMailbox]")
{
    auto wakePosts = 0;
    gx::MainThreadRequestMailbox mailbox{[&] { ++wakePosts; }};

    REQUIRE(mailbox.submitFrameWake("first"));
    (void)mailbox.drain();
    REQUIRE(mailbox.submitCapture(std::filesystem::path{"frame.png"}));

    const auto batch = mailbox.drain();
    REQUIRE(batch.captures.size() == 1);
    CHECK(batch.captures[0] == std::filesystem::path{"frame.png"});
    CHECK(batch.frameWake);
    CHECK(wakePosts == 2);
}

TEST_CASE("MainThreadRequestMailbox can disable OS wake posting while still recording work",
          "[MainThreadRequestMailbox]")
{
    auto wakePosts = 0;
    gx::MainThreadRequestMailbox mailbox{[&] { ++wakePosts; }};
    mailbox.setWakePostingEnabled(false);

    CHECK_FALSE(mailbox.submitClose());
    CHECK(mailbox.hasPendingWork());

    const auto batch = mailbox.drain();
    CHECK(batch.closeRequested);
    CHECK(wakePosts == 0);
}
