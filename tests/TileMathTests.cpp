#include "catch.hpp"

#include "../src/Formula/FormulaCompiler.h"
#include "../src/Tile/TileMath.h"

namespace
{
gx::ViewportRequest hugeRequest(const gx::CompiledFormulaHandle &formula)
{
    return {
        .header = {.requestId = 1, .generation = 1},
        .formula = formula,
        .xRange = Interval{-1.0e12, 1.0e12},
        .yRange = Interval{-1.0e12, 1.0e12},
        .framebufferWidth = 1600,
        .framebufferHeight = 1000,
        .devicePixelRatio = 1.0
    };
}
}

TEST_CASE("TileMath seed level is not capped by the old finite-grid level", "[TileMath]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x<=y");
    REQUIRE(formula.diagnostics.ok);
    const auto request = hugeRequest(formula.handle);
    const auto seedLevel = gx::seedTileLevelForViewport(request);

    CHECK(seedLevel > 30);
    const auto seedCount = gx::tileCountForViewportAtLevel(request, seedLevel, 4);
    REQUIRE(seedCount);
    CHECK(*seedCount <= 4);

    const auto previousCount = gx::tileCountForViewportAtLevel(request, seedLevel - 1, 4);
    CHECK_FALSE(previousCount);
}

TEST_CASE("TileMath refinement depth follows the seed level at huge zoom-out", "[TileMath]")
{
    const auto formula = gx::FormulaCompiler{}.compile("x<=y");
    REQUIRE(formula.diagnostics.ok);
    const auto request = hugeRequest(formula.handle);
    const auto seedLevel = gx::seedTileLevelForViewport(request);

    CHECK(gx::leafTileLevel(request, 4, 3) == seedLevel - 3);
}
