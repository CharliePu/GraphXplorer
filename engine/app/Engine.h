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
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
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
        int detailLevel{0};    // pixel-scale level; Mixed nodes at/below it get the fine raster
        bool baseRaster{false}; // start-level Mixed node: render a coarse placeholder raster
    };

    void schedulerLoop();
    void workerLoop(int workerIndex);
    void enqueueVisible(const Viewport &vp, std::shared_ptr<const Relation> rel, uint64_t epoch,
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

    // job queue
    std::mutex jobMutex_;
    std::condition_variable jobCv_;
    std::deque<Job> jobs_;
    std::atomic<uint64_t> jobsCompleted_{0};
    std::atomic<int> jobsInFlight_{0};

    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> frameCounter_{0};
    std::thread schedulerThread_;
    std::vector<std::thread> workers_;
};
}

#endif // GXR_APP_ENGINE_H
