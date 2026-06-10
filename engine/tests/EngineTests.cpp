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

    // The compositor may discover tiles still needing a (re)solve on its first
    // pass (e.g. an intermediate classified for a transient viewport before
    // this one landed) -- it REQUESTS them; settle like a real frame loop until
    // stable, as the pan-storm test does. This MUST converge in a few cycles.
    std::vector<PresentTile> present;
    size_t visible = 0;
    for (int iter = 0; iter < 8; ++iter)
    {
        visible = engine.buildPresent(vp, present);
        bool refining = false;
        for (const PresentTile &p : present)
            if (p.fallback || p.state != TileState::Done)
            {
                refining = true;
                break;
            }
        if (!refining) break;
        engine.waitUntilQuiescent();
    }
    REQUIRE(visible > 0);
    REQUIRE_FALSE(present.empty());

    // every emitted leaf is final: a greedy flat (exact) or an own Done raster.
    for (const PresentTile &p : present)
    {
        REQUIRE_FALSE(p.fallback);
        REQUIRE(p.state == TileState::Done);
        REQUIRE((p.flat || p.cov != nullptr));
    }

    // a Mixed detail tile's raster matches a direct fine solve of the same rect
    // (the FINAL refine-ladder pass uses exactly these params -- see
    // refinePassParams -- so a converged engine tile is byte-identical).
    const PresentTile *detail = nullptr;
    for (const PresentTile &p : present)
        if (!p.flat && p.cov)
        {
            detail = &p;
            break;
        }
    REQUIRE(detail != nullptr);
    SolveParams fine{64, 4, 200'000, true}; // == engine final-pass params
    EvalScratch s;
    CoverageTile direct = solveTile(*r, detail->rect, fine, s);
    REQUIRE(direct.alpha == detail->cov->alpha);
}

TEST_CASE("OBJECTIVE 2 (input): main-thread present is bounded (O visible) and never blocks",
          "[engine][latency]")
{
    Engine engine(/*tilePx=*/64, /*numWorkers=*/4);

    // 2-D sub-pixel oscillation, no explicit-y structure: every tile is genuinely
    // budget-bound (slow) -> workers stay busy long after the call below returns.
    auto heavy = rel("sin(x*y) > 0");
    Viewport deep{200.0, 200.0, 0.05, 512, 512};
    engine.setRelation(heavy);
    engine.setViewport(deep);

    // Immediately, WITHOUT waiting for any solve: buildPresent returns every time
    // (never blocks on workers), and the work it performs is bounded by the
    // viewport, NOT by the formula's cost. (Deterministic: no wall-clock asserts.)
    std::vector<PresentTile> present;
    size_t maxEmitted = 0;
    for (int i = 0; i < 300; ++i)
        maxEmitted = std::max(maxEmitted, engine.buildPresent(deep, present));
    REQUIRE(maxEmitted > 0);
    REQUIRE(maxEmitted < 4000); // O(visible nodes), independent of formula complexity

    // Same viewport, trivial formula: also bounded (greedy makes it far smaller).
    Engine engine2(64, 4);
    auto trivial = rel("y > x");
    engine2.setRelation(trivial);
    engine2.setViewport(deep);
    engine2.waitUntilQuiescent();
    std::vector<PresentTile> p2;
    REQUIRE(engine2.buildPresent(deep, p2) < 4000);
}

TEST_CASE("OBJECTIVE 2 (generation): changing formula cancels stale work (storm safety)",
          "[engine][latency]")
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
    // store stayed bounded despite the churn (greedy quadtree + LRU eviction)
    REQUIRE(engine.storeSize() < 16000);
}

TEST_CASE("OBJECTIVE 2 (generation): after a pan storm the current viewport fully completes, bounded",
          "[engine][viewport]")
{
    Engine engine(64, 4);
    engine.setRelation(rel("sin(x*y) > 0")); // heavy: lots of detail tiles per viewport

    // Pan rapidly across many far-apart viewports (off-screen backlog piles up).
    for (int k = 0; k < 12; ++k)
    {
        engine.setViewport(Viewport{1500.0 * k, -800.0 * k, 0.05, 256, 256});
    }
    // Settle on a final viewport.
    Viewport finalVp{0.0, 0.0, 0.05, 256, 256};
    engine.setViewport(finalVp);
    engine.waitUntilQuiescent();

    // Viewport prioritization + off-screen cull => the CURRENT viewport is fully
    // generated (no fallback, every leaf Done), not stuck behind the stale backlog.
    // buildPresent itself may request re-solves of stuck detail tiles, so settle
    // until stable (this MUST converge in a few cycles -- a freeze would not).
    std::vector<PresentTile> present;
    int fb = 1;
    for (int iter = 0; iter < 8 && fb > 0; ++iter)
    {
        engine.waitUntilQuiescent();
        engine.buildPresent(finalVp, present);
        fb = 0;
        for (const PresentTile &p : present)
            if (p.fallback) ++fb;
    }
    REQUIRE_FALSE(present.empty());
    REQUIRE(fb == 0); // converged: the current viewport is fully generated
    for (const PresentTile &p : present) REQUIRE(p.state == TileState::Done);
    // off-screen detail work was culled at the source -> store never exploded.
    REQUIRE(engine.storeSize() < 16000);
}

TEST_CASE("eviction never removes the recently-touched working set (soft cap)", "[store]")
{
    TileStore store;
    for (int i = 0; i < 20; ++i)
    {
        TileKey k{1, 0, i, 0};
        store.ensureQueued(k);
        store.touch(k, static_cast<uint64_t>(i < 12 ? 100 : 1)); // 12 active, 8 old
    }
    // budget far below the active set: only the 8 old tiles may go; the store
    // stays ABOVE budget rather than cannibalizing what the compositor uses.
    store.evictToBudget(4, /*keepEpoch=*/1, /*protectAfterFrame=*/100);
    REQUIRE(store.size() == 12);
    for (int i = 0; i < 12; ++i) REQUIRE(store.state(TileKey{1, 0, i, 0}) != TileState::Missing);
}
