#ifndef GXR_APP_ENGINE_H
#define GXR_APP_ENGINE_H

#include "expr/Relation.h"
#include "solve/Solver.h"
#include "tile/Geometry.h"
#include "tile/TileKey.h"
#include "tile/TileStore.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace gxr
{
// One visible tile selected for presentation: its key, world extent, and the
// best currently-available immutable coverage snapshot (may be a coarser
// fallback, or null while the first pass is in flight).
struct PresentTile
{
    TileKey key;
    WorldRect rect;     // the SCREEN footprint to draw (always the leaf's own rect)
    CoverageTilePtr cov; // texture to sample (own tile, or an ancestor for fallback); null if flat
    int level{0};
    bool fallback{false};
    TileState state{TileState::Missing};
    bool flat{false};       // greedy uniform tile: draw a solid fill, no texture
    float flatValue{0.0f};  // 0 or 1
    float u0{0.0f}, v0{0.0f}, u1{1.0f}, v1{1.0f}; // texture sub-rect (for ancestor fallback)

    // Coarse-ancestor STAND-IN for a detail tile whose OWN texture may not be
    // GPU-resident yet. The presenter draws this (one shared, already-resident
    // ancestor payload -> zero new upload) instead of a hole, then upgrades to `cov`
    // once it uploads. Exactly one of {own cov, this stand-in} is drawn per frame --
    // never both (no double-blend). A proven-FALSE ancestor leaves both null/false ->
    // the presenter draws nothing -> background (correct for a false region).
    CoverageTilePtr standinCov;                      // ancestor raster; null if none/flat
    bool standinFlat{false};                         // true => proven-true ancestor: solid fill
    TileKey standinKey{};                             // the ancestor's key (set with standinCov)
    float su0{0.0f}, sv0{0.0f}, su1{1.0f}, sv1{1.0f}; // stand-in UV sub-rect
};

// One visible tile's address + solve state, for the debug overlay.
struct DebugTile
{
    WorldRect rect;
    TileState state{TileState::Missing};
};

// Async render engine. The main thread only calls setRelation/setViewport
// (cheap, non-blocking) and buildPresent (O(visible tiles), reads cached
// snapshots only). All solving happens on worker threads driven by a scheduler
// thread. Provably decoupled: main-thread cost does not scale with formula
// complexity or outstanding solver work.
class Engine
{
public:
    // `wake` (optional) is invoked from worker threads when a tile is published,
    // so an event-driven main loop blocked on glfwWaitEvents wakes to recomposite.
    // It is stored before any worker thread is spawned, so the read is race-free.
    explicit Engine(int tilePx = 64, int numWorkers = 0, std::function<void()> wake = {});
    ~Engine();

    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    // main thread, non-blocking
    void setRelation(std::shared_ptr<const Relation> rel);
    void setViewport(const Viewport &vp);

    // main thread: select visible tiles for vp from cache. O(visible tiles).
    // Returns the number of store snapshot lookups performed (== visible count).
    size_t buildPresent(const Viewport &vp, std::vector<PresentTile> &out);

    // main thread: visible tiles with their solve state, for the debug overlay.
    void debugTiles(const Viewport &vp, std::vector<DebugTile> &out);

    // diagnostics / proofs
    [[nodiscard]] size_t storeSize() const { return store_.size(); }
    [[nodiscard]] uint64_t jobsCompleted() const { return jobsCompleted_.load(); }
    [[nodiscard]] uint64_t currentEpoch() const { return epoch_.load(); }
    [[nodiscard]] int jobsInFlight() const { return jobsInFlight_.load(); }
    [[nodiscard]] uint64_t abortsArmed() const { return abortsArmed_.load(); }
    [[nodiscard]] const TileStore &storeView() const { return store_; } // read-only probes
    void waitUntilQuiescent(); // test helper: block until the job queue drains

    int tilePx() const { return tilePx_; }

private:
    struct Job
    {
        TileKey key;
        std::shared_ptr<const Relation> rel;
        uint64_t epoch;
        std::shared_ptr<std::atomic<bool>> cancel;
        WorldRect rect;
        int detailLevel{0};     // pixel-scale level; Mixed nodes at/below it get the fine raster
        bool baseRaster{false}; // start-level Mixed node: render a coarse placeholder raster
        uint64_t viewportGen{0}; // viewport generation this job was scheduled for (priority)
        uint64_t seq{0};         // FIFO tie-break within a priority tier
        int refinePass{0};       // progressive ladder pass (0 = classify + first paint)
        bool onScreen{true};     // strictly inside the viewport at enqueue time
    };

    // Priority tiers (MAX-heap):
    //   1. strictly-VISIBLE before the speculative pan-ahead ring (obj 2)
    //   2. FIRST PAINT (pass 0) before any refinement -> the screen fills at
    //      coarse quality before anything sharpens ("appear all first")
    //   3. newest viewport generation (recency among equals)
    //   4. coarsest level (the no-holes base/fallback layer fills first)
    //   5. earlier refine pass (the field sharpens one step together)
    //   6. oldest seq (FIFO)
    // Generation is deliberately NOT the top tier: whether a job should run at
    // all is decided by the wantsTile draw-model cull at dequeue, and a zoom
    // storm leaves the still-wanted discovery cascade of the FINAL viewport
    // carrying mid-storm generations -- gen-first would bury it under the
    // current generation's self-enqueued refines (first-paint starvation).
    // The main thread never touches this queue, so its O(log N) cost stays off
    // the responsiveness path.
    struct JobCmp
    {
        bool operator()(const Job &a, const Job &b) const
        {
            if (a.onScreen != b.onScreen) return a.onScreen < b.onScreen;
            const bool afp = a.refinePass == 0, bfp = b.refinePass == 0;
            if (afp != bfp) return afp < bfp;
            if (a.viewportGen != b.viewportGen) return a.viewportGen < b.viewportGen;
            if (a.key.level != b.key.level) return a.key.level < b.key.level;
            if (a.refinePass != b.refinePass) return a.refinePass > b.refinePass;
            return a.seq > b.seq;
        }
    };

    // A worker's currently-executing job + its abort flag, so setViewport can
    // cancel in-flight work whose output the latest viewport will not draw.
    struct Inflight
    {
        TileKey key;
        WorldRect rect;
        int refinePass{0};
        std::shared_ptr<std::atomic<bool>> abort;
    };

    void schedulerLoop();
    void workerLoop(int workerIndex);
    void selfEnqueueRefine(const Job &done);
    void registerInflight(const Job &job, const std::shared_ptr<std::atomic<bool>> &abort);
    void unregisterInflight(const TileKey &key);
    // Arm the abort flag of in-flight jobs the latest viewport will not draw
    // (scheduler thread; see the wantsTile draw-model predicate in Engine.cpp).
    void abandonStaleInflight();
    // Rebuild the queue against the latest viewport: drop never-drawn jobs
    // (abandoning their slots) and refresh survivors' priority flags, which go
    // stale the moment the user moves (scheduler thread).
    void requeueForViewport(const Viewport &vp);
    void enqueueVisible(const Viewport &vp, std::shared_ptr<const Relation> rel, uint64_t epoch,
                        const std::shared_ptr<std::atomic<bool>> &cancel);
    void serviceResolve(const std::vector<TileKey> &keys, const Viewport &vp,
                        const std::shared_ptr<const Relation> &rel, uint64_t epoch,
                        const std::shared_ptr<std::atomic<bool>> &cancel);
    void pushJobs(std::vector<Job> &jobs);
    // greedy quadtree: coarsest level whose nodes cover the viewport in a few cells
    [[nodiscard]] int chooseStartLevel(const Viewport &vp) const;
    [[nodiscard]] std::vector<TileKey> startNodes(const Viewport &vp, uint64_t epoch) const;

    int tilePx_;
    std::function<void()> wake_; // set in ctor before threads spawn
    TileStore store_;

    // mailbox (latest-wins): viewport + relation + epoch
    std::mutex mailMutex_;
    std::condition_variable mailCv_;
    Viewport mailViewport_{};
    std::shared_ptr<const Relation> mailRelation_;
    bool mailDirty_{false};
    std::atomic<uint64_t> epoch_{0};
    std::shared_ptr<std::atomic<bool>> liveCancel_;

    // Resolve requests: detail-level tiles the compositor needs but that are stuck
    // (an intermediate Coarse node reused at a coarser zoom, or a culled Missing
    // node back in view). buildPresent appends keys; the scheduler claims+enqueues
    // them with a consistent relation/epoch/cancel. Guarded by mailMutex_.
    std::vector<TileKey> resolveReq_;
    bool resolvePending_{false};
    // Detail level the compositor used when it queued the current resolve requests,
    // so the scheduler solves them at the RIGHT level: a detail tile (level==detail)
    // gets its fine raster + Done; an Unknown intermediate node (level>detail) gets
    // classified and CASCADES its children down to the detail level (this is what
    // lets a deep zoom continue past nodes a shallower prior view already classified).
    std::atomic<int> resolveDetail_{0};

    // Latest viewport, published for worker-side culling of off-screen work, and a
    // generation counter bumped on every viewport change for priority ordering.
    std::atomic<std::shared_ptr<const Viewport>> currentVp_;
    std::atomic<uint64_t> viewportGen_{0};

    // job queue (viewport-prioritized)
    std::mutex jobMutex_;
    std::condition_variable jobCv_;
    std::priority_queue<Job, std::vector<Job>, JobCmp> jobs_;
    std::atomic<uint64_t> jobSeq_{0};
    std::atomic<uint64_t> jobsCompleted_{0};
    std::atomic<int> jobsInFlight_{0};

    // in-flight registry (<= one entry per worker), for viewport-change aborts
    std::mutex inflightMutex_;
    std::vector<Inflight> inflight_;
    std::atomic<uint64_t> abortsArmed_{0}; // diagnostic: aborts requested so far

    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> frameCounter_{0};
    std::thread schedulerThread_;
    std::vector<std::thread> workers_;
};
}

#endif // GXR_APP_ENGINE_H
