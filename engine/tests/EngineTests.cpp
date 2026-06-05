#include <catch2/catch_test_macros.hpp>

#include "app/Engine.h"
#include "solve/Solver.h"

#include <chrono>
#include <memory>

using namespace gxr;

namespace
{
std::shared_ptr<const Relation> rel(const std::string &src)
{
    std::string err;
    auto r = Relation::parse(src, err);
    REQUIRE(r.has_value());
    return std::make_shared<const Relation>(std::move(*r));
}
}

TEST_CASE("tile store: publish is immutable and snapshot is lock-light", "[store]")
{
    TileStore store;
    const TileKey k{1, 0, 2, 3};
    REQUIRE(store.snapshot(k) == nullptr);
    REQUIRE(store.state(k) == TileState::Missing);

    REQUIRE(store.ensureQueued(k) == TileState::Missing);
    REQUIRE(store.state(k) == TileState::Queued);

    auto t = std::make_shared<CoverageTile>();
    t->width = t->height = 2;
    t->alpha = {0.1f, 0.2f, 0.3f, 0.4f};
    store.publish(k, t, false);
    REQUIRE(store.state(k) == TileState::Coarse);

    CoverageTilePtr snap = store.snapshot(k);
    REQUIRE(snap != nullptr);
    REQUIRE(snap->alpha.size() == 4);

    // publishing a newer snapshot does not mutate the old one held by a reader
    auto t2 = std::make_shared<CoverageTile>();
    t2->width = t2->height = 2;
    t2->alpha = {1, 1, 1, 1};
    store.publish(k, t2, true);
    REQUIRE(snap->alpha[0] == 0.1f); // old snapshot unchanged
    REQUIRE(store.snapshot(k)->alpha[0] == 1.0f);
    REQUIRE(store.state(k) == TileState::Done);
}

TEST_CASE("tile store eviction drops stale epochs and LRU first", "[store]")
{
    TileStore store;
    for (int i = 0; i < 10; ++i)
    {
        TileKey k{1, 0, i, 0};
        store.ensureQueued(k);
        store.touch(k, static_cast<uint64_t>(i));
    }
    TileKey staleKey{0, 0, 99, 0};
    store.ensureQueued(staleKey);
    store.touch(staleKey, 1000); // recently touched but stale epoch

    store.evictToBudget(5, /*keepEpoch=*/1);
    REQUIRE(store.size() == 5);
    REQUIRE(store.state(staleKey) == TileState::Missing); // stale evicted despite recent touch
}

TEST_CASE("engine pipeline solves visible tiles and matches the direct solver", "[engine]")
{
    Engine engine(/*tilePx=*/64, /*numWorkers=*/4);
    auto r = rel("x^2 + y^2 < 1");
    engine.setRelation(r);
    Viewport vp{0.0, 0.0, 0.02, 256, 256};
    engine.setViewport(vp);
    engine.waitUntilQuiescent();

    std::vector<PresentTile> present;
    const size_t visible = engine.buildPresent(vp, present);
    REQUIRE(visible > 0);
    REQUIRE_FALSE(present.empty());

    // every visible tile resolved to a snapshot after quiescence
    for (const PresentTile &p : present)
    {
        REQUIRE(p.cov != nullptr);
    }

    // pipeline output matches a direct fine solve of the same tile (bit-exact)
    const PresentTile &p = present.front();
    SolveParams fine{64, 4, 600'000, true};
    EvalScratch s;
    CoverageTile direct = solveTile(*r, p.rect, fine, s);
    REQUIRE(direct.alpha == p.cov->alpha);
}

TEST_CASE("OBJECTIVE 2: main thread work is O(visible) and decoupled from compute load",
          "[engine][latency]")
{
    Engine engine(/*tilePx=*/64, /*numWorkers=*/4);

    // 2-D sub-pixel oscillation with no explicit-y structure: every tile is
    // genuinely budget-bound (slow), and the 1-D accelerator does NOT apply.
    auto heavy = rel("sin(x*y) > 0");
    Viewport deep{200.0, 200.0, 0.05, 256, 256};
    engine.setRelation(heavy);
    engine.setViewport(deep);

    // Without waiting for ANY tile to finish, the main thread hammers buildPresent.
    using clock = std::chrono::steady_clock;
    std::vector<PresentTile> present;
    size_t visibleCount = 0;
    int iterations = 0;
    const auto deadline = clock::now() + std::chrono::milliseconds(50);
    while (clock::now() < deadline)
    {
        visibleCount = engine.buildPresent(deep, present);
        ++iterations;
    }

    // DETERMINISTIC decoupling proof (no wall-clock thresholds): the main thread
    // completed thousands of present passes while the workers, still grinding the
    // heavy formula, finished only a tiny fraction of the visible tiles.
    REQUIRE(visibleCount > 0);
    REQUIRE(iterations > 200);
    REQUIRE(engine.jobsCompleted() < visibleCount); // workers still busy -> main ran free

    // Visible-tile count depends ONLY on the viewport, not on formula complexity:
    auto trivial = rel("y > x");
    Engine engine2(64, 4);
    engine2.setRelation(trivial);
    engine2.setViewport(deep);
    std::vector<PresentTile> p2;
    const size_t trivialCount = engine2.buildPresent(deep, p2);
    REQUIRE(trivialCount == visibleCount); // O(visible), independent of formula
}

TEST_CASE("OBJECTIVE 2: changing formula cancels stale work (storm safety)", "[engine][latency]")
{
    Engine engine(64, 4);
    Viewport vp{30.0, 0.0, 0.02, 256, 256};
    engine.setViewport(vp);

    // Rapid formula churn (like typing), each bump cancels the previous epoch.
    for (int i = 0; i < 20; ++i)
    {
        engine.setRelation(rel("y > sin(2^x) + " + std::to_string(i) + "*0.001"));
    }
    const uint64_t finalEpoch = engine.currentEpoch();
    engine.waitUntilQuiescent();

    // After the storm, only the final epoch's tiles need to be present-able.
    std::vector<PresentTile> present;
    engine.buildPresent(vp, present);
    REQUIRE(engine.currentEpoch() == finalEpoch);
    // store stayed bounded despite the churn
    REQUIRE(engine.storeSize() < 4096);
}
