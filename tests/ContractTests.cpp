#include "catch.hpp"

#include "../src/Util/Contracts.h"

TEST_CASE("TileTransaction validates schema, request, generation, and formula identity", "[Contracts]")
{
    const gx::FormulaSemanticsHash semantics{42};
    gx::TileTransaction tx{
        .header = {.requestId = 7, .generation = 9},
        .semanticsHash = semantics,
        .deltas = {
            gx::TileDelta{
                .header = {.requestId = 7, .generation = 9},
                .semanticsHash = semantics,
                .key = {.x = 1, .y = 2, .level = 3},
                .stage = gx::TileStage::IntervalReady,
                .classification = gx::TileClassification::Mixed,
                .interval = Interval{0.0, 1.0}
            }
        }
    };

    CHECK(tx.valid());
    CHECK(gx::toJsonSnapshot(tx).find("\"requestId\":7") != std::string::npos);

    tx.deltas.front().header.requestId = 8;
    CHECK_FALSE(tx.valid());
}

TEST_CASE("ViewportRequest validates framebuffer and range contracts", "[Contracts]")
{
    gx::ViewportRequest request{
        .header = {.requestId = 1, .generation = 1},
        .formula = {
            .id = 1,
            .version = 1,
            .semanticsHash = gx::FormulaSemanticsHash{99}
        },
        .xRange = Interval{-1.0, 1.0},
        .yRange = Interval{-1.0, 1.0},
        .framebufferWidth = 800,
        .framebufferHeight = 600,
        .devicePixelRatio = 1.0
    };

    CHECK(request.valid());

    request.framebufferWidth = 0;
    CHECK_FALSE(request.valid());
}
