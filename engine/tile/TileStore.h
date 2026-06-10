#ifndef GXR_TILE_TILESTORE_H
#define GXR_TILE_TILESTORE_H

#include "TileKey.h"
#include "solve/Coverage.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace gxr
{
enum class TileState : uint8_t
{
    Missing, // not yet known (or abandoned; re-claimable)
    Queued,  // scheduled or in flight
    Coarse,  // a snapshot is available, refinement ongoing
    Done,    // converged / final at requested quality
};

// Thread-safe pyramid cache. Reads (main-thread `snapshot`) take a shared lock
// plus an atomic load of an immutable CoverageTilePtr -> never blocked by, and
// never tear against, worker publishes. Structural inserts take the unique lock.
class TileStore
{
    struct Slot
    {
        std::atomic<CoverageTilePtr> snap{nullptr};
        std::atomic<TileState> state{TileState::Missing};
        std::atomic<NodeClass> klass{NodeClass::Unknown};
        std::atomic<uint64_t> lastTouched{0};
        // Highest refinement-ladder pass already published (-1 = none). The
        // single no-downgrade authority: a stale coarser publish can never
        // overwrite a finer raster, regardless of completion order.
        std::atomic<int> bestPass{-1};
    };

public:
    // Main-thread read: current immutable snapshot, or nullptr. O(1), lock-light.
    [[nodiscard]] CoverageTilePtr snapshot(const TileKey &key) const;

    [[nodiscard]] TileState state(const TileKey &key) const;

    // Ensure a slot exists and is claimed: a Missing slot (existing or newly
    // created) transitions to Queued. Returns the state observed BEFORE the call
    // (Missing => this caller claimed it and must enqueue the job).
    TileState ensureQueued(const TileKey &key);

    // Worker publishes a refined snapshot. The slot must already exist.
    void publish(const TileKey &key, CoverageTilePtr cov, bool done);

    // Ladder-aware publish with the no-downgrade guarantee: stores `cov` only if
    // `pass` is strictly newer than the slot's bestPass. Returns false (and drops
    // cov) on a stale publish racing a finer one.
    bool publishRefine(const TileKey &key, CoverageTilePtr cov, int pass, bool done);

    // Highest published ladder pass for the key (-1 if none / no slot).
    [[nodiscard]] int bestPass(const TileKey &key) const;

    // Greedy quadtree node classification (proven uniform vs mixed).
    void setClass(const TileKey &key, NodeClass c);
    [[nodiscard]] NodeClass classOf(const TileKey &key) const;

    // Claim a stuck node for (re)solving: atomically transition Coarse->Queued or
    // Missing->Queued (returns true if this caller claimed it). Used to re-solve a
    // tile that was an intermediate node at a finer zoom (Coarse, no raster), was
    // culled/abandoned (Missing), or whose refine chain broke. A node already
    // Queued or Done is not re-claimed (so it enqueues exactly once).
    bool claimForResolve(const TileKey &key);

    // Abandon a not-yet-finished node: CAS Queued/Coarse -> Missing so it can be
    // re-claimed if needed again. NEVER touches Done and never clears the
    // snapshot (the last good raster stays drawable -- no downgrade on screen).
    void abandonIfUnfinished(const TileKey &key);

    void touch(const TileKey &key, uint64_t frame);

    // Evict least-recently-touched tiles when over `maxTiles`, down to a low
    // watermark, preferring tiles from epochs older than `keepEpoch`. Tiles
    // touched at or after `protectAfterFrame` are NEVER evicted: the compositor
    // touches exactly the active working set every frame, so this makes the
    // budget a SOFT cap that trims history but cannot thrash the current view
    // (a working set larger than the budget -- huge window, deep level -- keeps
    // the store above budget instead of evict/re-solve cycling forever). The
    // scan and ordering run OUTSIDE the unique lock so readers are stalled only
    // for the erase itself.
    void evictToBudget(size_t maxTiles, uint64_t keepEpoch,
                       uint64_t protectAfterFrame = UINT64_MAX);

    [[nodiscard]] size_t size() const;

    // ---- bulk read access (main-thread compositor walk) ---------------------
    // Holds the shared lock ONCE for a whole traversal; `find` resolves a key to
    // a Handle (ONE hash lookup), and the Handle reads the slot's atomics
    // directly. This keeps buildPresent at one lock + one find per visited node
    // instead of one lock+find per FIELD per node.
    class Handle
    {
    public:
        Handle() = default;
        explicit Handle(Slot *s) : s_(s) {}
        explicit operator bool() const { return s_ != nullptr; }
        [[nodiscard]] CoverageTilePtr snapshot() const
        {
            return s_ ? s_->snap.load(std::memory_order_acquire) : nullptr;
        }
        [[nodiscard]] TileState state() const
        {
            return s_ ? s_->state.load(std::memory_order_acquire) : TileState::Missing;
        }
        [[nodiscard]] NodeClass klass() const
        {
            return s_ ? s_->klass.load(std::memory_order_acquire) : NodeClass::Unknown;
        }
        [[nodiscard]] int bestPass() const
        {
            return s_ ? s_->bestPass.load(std::memory_order_acquire) : -1;
        }
        void touch(uint64_t frame) const
        {
            if (s_) s_->lastTouched.store(frame, std::memory_order_relaxed);
        }

    private:
        Slot *s_{nullptr};
    };

    class ReadAccess
    {
    public:
        explicit ReadAccess(const TileStore &store) : store_(store), lock_(store.mutex_) {}
        [[nodiscard]] Handle find(const TileKey &key) const
        {
            const auto it = store_.map_.find(key);
            return Handle{it == store_.map_.end() ? nullptr : it->second.get()};
        }

    private:
        const TileStore &store_;
        std::shared_lock<std::shared_mutex> lock_;
    };

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<TileKey, std::unique_ptr<Slot>> map_;
};
}

#endif // GXR_TILE_TILESTORE_H
