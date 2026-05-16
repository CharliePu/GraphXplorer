#include "catch.hpp"

#include "../src/Formula/Formula.h"
#include "../src/Graph/Graph.h"
#include "../src/Math/GraphProcessor.h"
#include "../src/Math/GraphRasterizer.h"
#include "../src/Util/ThreadPool.h"

TEST_CASE("GraphProcessor exits before work when cancelled", "[Compute][Cancellation]")
{
    const auto graph = std::make_shared<Graph>();
    const auto formula = std::make_shared<Formula>("x^2+y^2<4^2");
    const auto pool = std::make_shared<ThreadPool>(1);

    GraphProcessor processor(nullptr, pool);
    processor.process(
        graph,
        formula,
        Interval{-20.0, 20.0},
        Interval{-20.0, 20.0},
        800,
        800,
        [] { return true; });

    CHECK(graph->tiles.empty());
    CHECK(graph->activeLevels.empty());
}

TEST_CASE("GraphRasterizer exits before work when cancelled", "[Compute][Cancellation]")
{
    const auto graph = std::make_shared<Graph>();
    const auto formula = std::make_shared<Formula>("x^2+y^2<4^2");
    const auto pool = std::make_shared<ThreadPool>(1);

    graph->tiles.insert_or_assign(TileKey{0, 0, 0}, Tile{Interval{1.0, 1.0}});
    graph->activeLevels.insert(0);

    GraphRasterizer rasterizer(nullptr, pool);
    const auto plot = rasterizer.rasterize(
        graph,
        formula,
        Interval{0.0, 1.0},
        Interval{0.0, 1.0},
        256,
        256,
        [] { return true; });

    CHECK(plot.chunkRenderData.empty());
}
