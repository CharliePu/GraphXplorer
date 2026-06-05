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
    WorldRect rect;
    CoverageTilePtr cov;
    int level{0};
    bool fallback{false};
};

// Async render engine. The main thread only calls setRelation/setViewport
// (cheap, non-blocking) and buildPresent (O(visible tiles), reads cached
// snapshots only). All solving happens on worker threads driven by a scheduler
// thread. Provably decoupled: main-thread cost does not scale with formula
// complexity or outstanding solver work.
class Engine
{
public:
    explicit Engine(int tilePx = 64, int numWorkers = 0);
    ~Engine();

    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    // main thread, non-blocking
    void setRelation(std::shared_ptr<const Relation> rel);
    void setViewport(const Viewport &vp);

    // main thread: select visible tiles for vp from cache. O(visible tiles).
    // Returns the number of store snapshot lookups performed (== visible count).
    size_t buildPresent(const Viewport &vp, std::vector<PresentTile> &out);

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
    };

    void schedulerLoop();
    void workerLoop(int workerIndex);
    void enqueueVisible(const Viewport &vp, std::shared_ptr<const Relation> rel, uint64_t epoch,
                        const std::shared_ptr<std::atomic<bool>> &cancel);
    std::vector<TileKey> visibleKeys(const Viewport &vp, uint64_t epoch) const;

    int tilePx_;
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
