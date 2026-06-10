// gxrepro_prio: headless harness for objective 2 (input + generation latency) on
// the DEGENERATE general-path formula  y > sin(2^x) + y*0  (the +y*0 defeats the
// explicit-1D structure detection, so every Mixed tile pays the full 2-D
// subdivision + sampled-measure cost -- the worst case the scheduler must absorb).
//
// Scenarios (all wall-clock, all on one "main" thread that ticks buildPresent
// like the GUI event loop would):
//   cold     open directly on the oscillation wall; time first-paint-all /
//            all-sharp for the visible detail tiles.
//   zoomin   start solving a medium view, then scroll-storm 25 steps deeper
//            into the wall (one setViewport per ~16ms, like a real wheel);
//            time the FINAL viewport's first-paint-all / all-sharp from the
//            moment the last scroll step lands.
//   panfar   from the settled wall view, pan far away mid-solve; same metrics
//            for the new region (measures how fast stale in-flight work yields).
//
// Reported per scenario: tFirstPaint (every visible detail tile has its own
// raster or a proven-uniform ancestor), tAllSharp (every visible detail tile is
// Done or uniform-covered), buildPresent µs avg/max while workers grind, jobs,
// store size. A scenario that times out with zero progress prints *** FROZEN ***
// and the tool exits nonzero (regression tripwire).

#include "app/Engine.h"
#include "solve/Solver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace gxr;
using Clock = std::chrono::steady_clock;

namespace
{
constexpr int kTilePx = 64;
constexpr const char *kFormula = "y > sin(2^x) + y*0";

double ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

std::shared_ptr<const Relation> parseRel(const std::string &src)
{
    std::string err;
    auto r = Relation::parse(src, err);
    if (!r)
    {
        std::fprintf(stderr, "parse error: %s\n", err.c_str());
        std::exit(2);
    }
    return std::make_shared<const Relation>(std::move(*r));
}

// The visible detail-tile probe: mirrors the compositor's leaf semantics without
// depending on PresentTile plumbing. A detail key counts as
//   painted: it has ANY own snapshot, or a proven-uniform ancestor covers it;
//   sharp:   its state is Done, or a proven-uniform ancestor covers it.
struct Probe
{
    int total{0};
    int unpainted{0};
    int notSharp{0};
};

Probe probeVisible(const Engine &eng, const Viewport &vp)
{
    const TileStore &store = eng.storeView();
    const uint64_t epoch = eng.currentEpoch();
    const int detail = vp.activeLevel();
    const double span = tileSpanWorld(detail, kTilePx);
    const WorldRect r = vp.worldBounds();
    const int64_t i0 = floorDiv(r.x0, span), i1 = floorDiv(r.x1, span);
    const int64_t j0 = floorDiv(r.y0, span), j1 = floorDiv(r.y1, span);

    Probe p;
    for (int64_t j = j0; j <= j1; ++j)
        for (int64_t i = i0; i <= i1; ++i)
        {
            ++p.total;
            // a proven-uniform ancestor (or the node itself) is exact at any zoom
            bool uniform = false;
            TileKey k{epoch, detail, i, j};
            for (int up = 0; up <= 20 && !uniform; ++up)
            {
                const NodeClass c = store.classOf(k);
                if (c == NodeClass::UniformTrue || c == NodeClass::UniformFalse) uniform = true;
                k = TileKey{k.epoch, k.level + 1, k.i >> 1, k.j >> 1};
            }
            if (uniform) continue;
            const TileKey dk{epoch, detail, i, j};
            if (!store.snapshot(dk)) ++p.unpainted;
            if (store.state(dk) != TileState::Done) ++p.notSharp;
        }
    return p;
}

struct RunStats
{
    double tFirstPaint{-1.0};
    double tAllSharp{-1.0};
    double bpAvgMs{0.0};
    double bpMaxMs{0.0};
    long frames{0};
    bool frozen{false};
};

// Tick like the GUI loop until the visible set is sharp (or timeout), sampling
// buildPresent cost and the probe. `trace` prints a progress timeline.
RunStats runUntilSharp(Engine &eng, const Viewport &vp, double timeoutMs, bool trace = false)
{
    RunStats st;
    std::vector<PresentTile> present;
    const auto t0 = Clock::now();
    double bpSum = 0.0, lastTrace = 0.0;
    Probe last{};
    for (;;)
    {
        const auto f0 = Clock::now();
        eng.buildPresent(vp, present);
        const auto f1 = Clock::now();
        const double bp = ms(f0, f1);
        bpSum += bp;
        st.bpMaxMs = std::max(st.bpMaxMs, bp);
        ++st.frames;

        last = probeVisible(eng, vp);
        const double t = ms(t0, Clock::now());
        if (st.tFirstPaint < 0 && last.unpainted == 0) st.tFirstPaint = t;
        if (trace && (t - lastTrace > 250.0))
        {
            lastTrace = t;
            std::printf("  t=%-7.0f unpainted=%-4d notSharp=%-4d inflight=%-3d jobs=%llu store=%zu aborts=%llu\n",
                        t, last.unpainted, last.notSharp, eng.jobsInFlight(),
                        static_cast<unsigned long long>(eng.jobsCompleted()), eng.storeSize(),
                        static_cast<unsigned long long>(eng.abortsArmed()));
        }
        if (last.notSharp == 0)
        {
            st.tAllSharp = t;
            break;
        }
        if (t > timeoutMs) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    st.bpAvgMs = st.frames ? bpSum / st.frames : 0.0;
    st.frozen = (st.tAllSharp < 0 && last.notSharp >= last.total && last.total > 0);
    return st;
}

void report(const char *name, const Viewport &vp, const Engine &eng, const RunStats &st)
{
    std::printf("%-8s level=%-4d tiles: firstPaint=%-8.0fms allSharp=%-8.0fms "
                "bp(avg=%.2fms max=%.2fms frames=%ld) jobs=%llu inflight=%d store=%zu%s\n",
                name, vp.activeLevel(), st.tFirstPaint, st.tAllSharp, st.bpAvgMs, st.bpMaxMs,
                st.frames, static_cast<unsigned long long>(eng.jobsCompleted()),
                eng.jobsInFlight(), eng.storeSize(), st.frozen ? "  *** FROZEN ***" : "");
}

// Inherent per-tile cost reference: one direct fine solve of a wall detail tile.
void printTileCost(const Relation &rel, const Viewport &vp)
{
    EvalScratch s;
    const int detail = vp.activeLevel();
    const double span = tileSpanWorld(detail, kTilePx);
    const WorldRect r{25.0, 0.0, 25.0 + span, span}; // on the wall, straddling y in osc band
    const SolveParams fine{kTilePx, 4, 200'000, true};
    const SolveParams coarse{kTilePx, 1, 20'000, true};
    auto t0 = Clock::now();
    CoverageTile cf = solveTile(rel, r, fine, s);
    auto t1 = Clock::now();
    CoverageTile cc = solveTile(rel, r, coarse, s);
    auto t2 = Clock::now();
    std::printf("tilecost  fine(sub=4,200k)=%.1fms  coarse(sub=1,20k)=%.1fms  (one wall tile, level %d)\n",
                ms(t0, t1), ms(t1, t2), detail);
    (void)cf;
    (void)cc;
}
}

int main()
{
    std::printf("gxrepro_prio: \"%s\"  (general 2-D path, oscillation wall x~[20,30])\n", kFormula);
    auto rel = parseRel(kFormula);

    const int W = 1280, H = 720;
    bool anyFrozen = false;

    // ---- cold open on the wall --------------------------------------------
    {
        Engine eng(kTilePx);
        eng.setRelation(rel);
        Viewport vp{25.0, 0.0, 0.01, W, H};
        printTileCost(*rel, vp);
        eng.setViewport(vp);
        const RunStats st = runUntilSharp(eng, vp, 90'000);
        report("cold", vp, eng, st);
        anyFrozen |= st.frozen;
    }

    // ---- scroll-storm zoom-in mid-solve ------------------------------------
    {
        Engine eng(kTilePx);
        eng.setRelation(rel);
        Viewport vp{25.0, 0.0, 0.01, W, H};
        eng.setViewport(vp);
        // grind the medium view for 1.5s (NOT settled), like a user pausing
        std::vector<PresentTile> present;
        const auto g0 = Clock::now();
        while (ms(g0, Clock::now()) < 1500.0)
        {
            eng.buildPresent(vp, present);
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        // 25 wheel steps, one per ~16ms, zooming toward (25.03, 0.2)
        for (int k = 0; k < 25; ++k)
        {
            vp.worldPerPixel /= 1.1;
            eng.setViewport(vp);
            eng.buildPresent(vp, present);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        const RunStats st = runUntilSharp(eng, vp, 90'000, /*trace=*/true);
        report("zoomin", vp, eng, st);
        anyFrozen |= st.frozen;
    }

    // ---- pan far mid-solve --------------------------------------------------
    {
        Engine eng(kTilePx);
        eng.setRelation(rel);
        Viewport vp{25.0, 0.0, 0.01, W, H};
        eng.setViewport(vp);
        std::vector<PresentTile> present;
        const auto g0 = Clock::now();
        while (ms(g0, Clock::now()) < 1500.0)
        {
            eng.buildPresent(vp, present);
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        vp.centerX = 27.5; // far pan along the wall: all-new tiles, old ones off-screen
        eng.setViewport(vp);
        const RunStats st = runUntilSharp(eng, vp, 90'000);
        report("panfar", vp, eng, st);
        anyFrozen |= st.frozen;
    }

    std::printf("(want: zoomin/panfar firstPaint within a few frames' worth of solving, "
                "bp max well under a frame)\n");
    return anyFrozen ? 1 : 0;
}
