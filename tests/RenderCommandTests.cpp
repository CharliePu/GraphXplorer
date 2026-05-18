#include "catch.hpp"

#include <array>
#include <vector>

#include "../src/Render/FrameCommandBuffer.h"
#include "../src/Render/RenderResourceManager.h"
#include "../src/Render/UploadPlanner.h"

TEST_CASE("FrameCommandBuffer sorts and freezes draw commands", "[Render]")
{
    gx::FrameCommandBuffer buffer;
    CHECK(buffer.add({.layer = gx::RenderLayer::Text, .pipeline = {2}, .sortKey = 2}));
    CHECK(buffer.add({.layer = gx::RenderLayer::Plot, .pipeline = {1}, .sortKey = 1}));

    buffer.freezeAndSort();

    REQUIRE(buffer.frozen());
    REQUIRE(buffer.commands().size() == 2);
    CHECK(buffer.commands().front().layer == gx::RenderLayer::Plot);
    CHECK_FALSE(buffer.add({}));
}

TEST_CASE("UploadPlanner respects texture budgets", "[Render]")
{
    gx::TileRecord record;
    record.key = {1, 1, 0};
    record.valueState = gx::TileValueState::Mixed;
    record.workState = gx::TileWorkState::RegionReady;
    record.regionPixels = gx::RegionImageRef{.id = 1, .width = 256, .height = 256};

    const gx::UploadBudget budget{
        .maxTextureBytesPerFrame = 1024,
        .maxBufferBytesPerFrame = 1024,
        .maxTextureSlicesPerFrame = 1,
        .maxTileInstanceUpdatesPerFrame = 1
    };

    const std::array records{record};
    const auto plan = gx::UploadPlanner{}.plan(records, budget);
    CHECK(plan.textureUploads.empty());
    CHECK(plan.budgetExhausted);
}

TEST_CASE("UploadPlanner prioritizes visible display tiles", "[Render]")
{
    const gx::TileKey visibleKey{0, 0, 0};
    const gx::TileKey residentKey{1, 0, 0};
    const std::array tiles{
        gx::DisplayTile{
            .sourceKey = visibleKey,
            .visualState = gx::TileVisualState::MixedRegion,
            .cpuRegion = gx::RegionImageRef{.id = 1, .width = 256, .height = 256}
        },
        gx::DisplayTile{
            .sourceKey = residentKey,
            .visualState = gx::TileVisualState::MixedRegion,
            .cpuRegion = gx::RegionImageRef{.id = 2, .width = 256, .height = 256},
            .gpuSlice = gx::TextureSlice{.textureId = 4, .slice = 3}
        }
    };
    const gx::UploadBudget budget{
        .maxTextureBytesPerFrame = 256 * 256,
        .maxTextureSlicesPerFrame = 1
    };

    const auto plan = gx::UploadPlanner{}.planVisible(tiles, budget);
    CHECK(plan.textureUploads == std::vector<gx::TileKey>{visibleKey});
    CHECK_FALSE(plan.budgetExhausted);
}

TEST_CASE("RenderProgress requests follow-up frames for GPU upload progress", "[Render]")
{
    gx::RenderProgress idle;
    CHECK_FALSE(idle.needsFollowupFrame());

    gx::RenderProgress uploaded{
        .regionUploadsThisFrame = 2,
        .regionUploadBytesThisFrame = 512
    };
    CHECK(uploaded.needsFollowupFrame());

    gx::RenderProgress pending{
        .pendingRegionUploadsAfterFrame = 3,
        .regionUploadStateObserved = true
    };
    CHECK(pending.needsFollowupFrame());

    idle.merge(uploaded);
    idle.merge(pending);
    CHECK(idle.regionUploadsThisFrame == 2);
    CHECK(idle.regionUploadBytesThisFrame == 512);
    CHECK(idle.pendingRegionUploadsAfterFrame == 3);
    CHECK(idle.needsFollowupFrame());
}

TEST_CASE("RenderResourceManager assigns slices for the whole visible region set", "[Render]")
{
    gx::RenderResourceManager resources;
    std::vector<gx::RegionImageRef> visibleRefs;
    visibleRefs.reserve(600);
    for (auto id = uint64_t{1}; id <= 600; ++id)
    {
        visibleRefs.push_back(gx::RegionImageRef{.id = id, .width = 1, .height = 1});
    }

    resources.beginRegionFrame(visibleRefs);

    const std::array pixel{uint8_t{255}};
    for (const auto &ref : visibleRefs)
    {
        const auto slice = resources.registerRegionImage(ref, pixel);
        CHECK(slice.textureId == resources.regionTextureSet().id);
        CHECK(slice.slice < visibleRefs.size());
    }
}

TEST_CASE("RenderResourceManager reuses low free region slices after visibility shrinks", "[Render]")
{
    gx::RenderResourceManager resources;
    std::vector<gx::RegionImageRef> largeVisibleRefs;
    largeVisibleRefs.reserve(600);
    for (auto id = uint64_t{1}; id <= 600; ++id)
    {
        largeVisibleRefs.push_back(gx::RegionImageRef{.id = id, .width = 1, .height = 1});
    }

    resources.beginRegionFrame(largeVisibleRefs);
    const std::array pixel{uint8_t{255}};
    for (const auto &ref : largeVisibleRefs)
    {
        (void)resources.registerRegionImage(ref, pixel);
    }

    const std::array smallVisibleRefs{
        gx::RegionImageRef{.id = 1001, .width = 1, .height = 1},
        gx::RegionImageRef{.id = 1002, .width = 1, .height = 1},
        gx::RegionImageRef{.id = 1003, .width = 1, .height = 1},
        gx::RegionImageRef{.id = 1004, .width = 1, .height = 1}
    };

    resources.beginRegionFrame(smallVisibleRefs);
    for (const auto &ref : smallVisibleRefs)
    {
        const auto slice = resources.registerRegionImage(ref, pixel);
        CHECK(slice.slice < smallVisibleRefs.size());
    }
}
