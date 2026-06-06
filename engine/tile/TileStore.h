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
    Missing, // not yet known
    Queued,  // scheduled, not started
    Coarse,  // a low-quality snapshot is available, refinement ongoing
    Done,    // converged / final at requested quality
};

// Thread-safe pyramid cache. Reads (main-thread `snapshot`) take a shared lock
// plus an atomic load of an immutable CoverageTilePtr -> never blocked by, and
// never tear against, worker publishes. Structural inserts take the unique lock.
class TileStore
{
public:
    // Main-thread read: current immutable snapshot, or nullptr. O(1), lock-light.
    [[nodiscard]] CoverageTilePtr snapshot(const TileKey &key) const;

    [[nodiscard]] TileState state(const TileKey &key) const;

    // Ensure a slot exists in the given state if currently Missing; returns the
    // state observed before this call (Missing if newly created).
    TileState ensureQueued(const TileKey &key);

    // Worker publishes a refined snapshot. The slot must already exist.
    void publish(const TileKey &key, CoverageTilePtr cov, bool done);

    // Greedy quadtree node classification (proven uniform vs mixed).
    void setClass(const TileKey &key, NodeClass c);
    [[nodiscard]] NodeClass classOf(const TileKey &key) const;

    // Claim a stuck node for (re)solving: atomically transition Coarse->Queued or
    // Missing->Queued (returns true if this caller claimed it). Used to re-solve a
    // tile that was an intermediate node at a finer zoom (Coarse, no raster) or was
    // culled off-screen (Missing) and is now needed at the detail level. A node
    // already Queued or Done is not re-claimed (so it enqueues exactly once).
    bool claimForResolve(const TileKey &key);

    // Reset a node to Missing (used when a worker culls an off-screen job, so it
    // can be re-scheduled if it later returns to view).
    void resetToMissing(const TileKey &key);

    void touch(const TileKey &key, uint64_t frame);

    // Evict least-recently-touched tiles down to `maxTiles`, preferring tiles
    // from epochs older than `keepEpoch`.
    void evictToBudget(size_t maxTiles, uint64_t keepEpoch);

    [[nodiscard]] size_t size() const;

private:
    struct Slot
    {
        std::atomic<CoverageTilePtr> snap{nullptr};
        std::atomic<TileState> state{TileState::Missing};
        std::atomic<NodeClass> klass{NodeClass::Unknown};
        std::atomic<uint64_t> lastTouched{0};
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<TileKey, std::unique_ptr<Slot>> map_;
};
}

#endif // GXR_TILE_TILESTORE_H
