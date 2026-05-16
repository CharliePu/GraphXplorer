#include "catch.hpp"

#include <array>
#include <vector>

#include "../src/Render/FrameCommandBuffer.h"
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
