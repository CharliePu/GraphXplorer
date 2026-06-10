#include "TileStore.h"

#include <algorithm>
#include <vector>

namespace gxr
{
CoverageTilePtr TileStore::snapshot(const TileKey &key) const
{
    std::shared_lock lock(mutex_);
    const auto it = map_.find(key);
    if (it == map_.end()) return nullptr;
    return it->second->snap.load(std::memory_order_acquire);
}

TileState TileStore::state(const TileKey &key) const
{
    std::shared_lock lock(mutex_);
    const auto it = map_.find(key);
    if (it == map_.end()) return TileState::Missing;
    return it->second->state.load(std::memory_order_acquire);
}

TileState TileStore::ensureQueued(const TileKey &key)
{
    {
        std::shared_lock lock(mutex_);
        const auto it = map_.find(key);
        if (it != map_.end())
        {
            // An existing slot that fell back to Missing (abandoned / culled) is
            // re-claimable here: CAS it to Queued and report Missing so the
            // caller enqueues a fresh job. Without this, a once-abandoned slot
            // was requeue-resistant (the cascade saw "not Missing" and skipped it
            // forever; only the compositor's stuck path could revive it).
            TileState expect = TileState::Missing;
            if (it->second->state.compare_exchange_strong(expect, TileState::Queued,
                                                          std::memory_order_acq_rel))
                return TileState::Missing;
            return expect; // the (non-Missing) state we lost the race to
        }
    }
    std::unique_lock lock(mutex_);
    auto [it, inserted] = map_.try_emplace(key, nullptr);
    if (inserted)
    {
        it->second = std::make_unique<Slot>();
        it->second->state.store(TileState::Queued, std::memory_order_release);
        return TileState::Missing;
    }
    TileState expect = TileState::Missing;
    if (it->second->state.compare_exchange_strong(expect, TileState::Queued,
                                                  std::memory_order_acq_rel))
        return TileState::Missing;
    return expect;
}

void TileStore::publish(const TileKey &key, CoverageTilePtr cov, bool done)
{
    std::shared_lock lock(mutex_);
    const auto it = map_.find(key);
    if (it == map_.end()) return; // slot evicted underneath us; drop
    it->second->snap.store(std::move(cov), std::memory_order_release);
    it->second->state.store(done ? TileState::Done : TileState::Coarse, std::memory_order_release);
}

bool TileStore::publishRefine(const TileKey &key, CoverageTilePtr cov, int pass, bool done)
{
    std::shared_lock lock(mutex_);
    const auto it = map_.find(key);
    if (it == map_.end()) return false; // slot evicted underneath us; drop
    Slot &slot = *it->second;
    // monotone bestPass: only a strictly finer pass may replace the raster
    int cur = slot.bestPass.load(std::memory_order_acquire);
    for (;;)
    {
        if (pass <= cur) return false; // stale/duplicate publish: no downgrade
        if (slot.bestPass.compare_exchange_weak(cur, pass, std::memory_order_acq_rel)) break;
    }
    slot.snap.store(std::move(cov), std::memory_order_release);
    slot.state.store(done ? TileState::Done : TileState::Coarse, std::memory_order_release);
    return true;
}

int TileStore::bestPass(const TileKey &key) const
{
    std::shared_lock lock(mutex_);
    const auto it = map_.find(key);
    if (it == map_.end()) return -1;
    return it->second->bestPass.load(std::memory_order_acquire);
}

void TileStore::setClass(const TileKey &key, NodeClass c)
{
    std::shared_lock lock(mutex_);
    const auto it = map_.find(key);
    if (it != map_.end()) it->second->klass.store(c, std::memory_order_release);
}

NodeClass TileStore::classOf(const TileKey &key) const
{
    std::shared_lock lock(mutex_);
    const auto it = map_.find(key);
    if (it == map_.end()) return NodeClass::Unknown;
    return it->second->klass.load(std::memory_order_acquire);
}

bool TileStore::claimForResolve(const TileKey &key)
{
    auto tryClaim = [](Slot &slot) {
        TileState e = TileState::Coarse;
        if (slot.state.compare_exchange_strong(e, TileState::Queued)) return true;
        e = TileState::Missing;
        if (slot.state.compare_exchange_strong(e, TileState::Queued)) return true;
        return false;
    };
    {
        std::shared_lock lock(mutex_);
        const auto it = map_.find(key);
        if (it != map_.end()) return tryClaim(*it->second);
    }
    // No slot yet: this detail tile is needed but the cascade never reached it
    // (its ancestor was already classified, so nothing enqueued it). Create the
    // slot, claimed, so the scheduler enqueues a fresh classify+raster job for it.
    std::unique_lock lock(mutex_);
    auto [it, inserted] = map_.try_emplace(key, nullptr);
    if (inserted)
    {
        it->second = std::make_unique<Slot>();
        it->second->state.store(TileState::Queued, std::memory_order_release);
        return true;
    }
    return tryClaim(*it->second);
}

void TileStore::abandonIfUnfinished(const TileKey &key)
{
    std::shared_lock lock(mutex_);
    const auto it = map_.find(key);
    if (it == map_.end()) return;
    Slot &slot = *it->second;
    TileState e = TileState::Queued;
    if (slot.state.compare_exchange_strong(e, TileState::Missing)) return;
    e = TileState::Coarse;
    slot.state.compare_exchange_strong(e, TileState::Missing);
    // Done is never demoted; the snapshot (if any) is never cleared.
}

void TileStore::touch(const TileKey &key, uint64_t frame)
{
    std::shared_lock lock(mutex_);
    const auto it = map_.find(key);
    if (it != map_.end()) it->second->lastTouched.store(frame, std::memory_order_relaxed);
}

void TileStore::evictToBudget(size_t maxTiles, uint64_t keepEpoch)
{
    struct Ref
    {
        TileKey key;
        uint64_t touched;
        bool stale;
    };
    std::vector<Ref> refs;
    {
        // Scan under the SHARED lock: readers (the compositor walk) are not stalled.
        std::shared_lock lock(mutex_);
        if (map_.size() <= maxTiles) return;
        refs.reserve(map_.size());
        for (const auto &[k, slot] : map_)
            refs.push_back({k, slot->lastTouched.load(std::memory_order_relaxed), k.epoch < keepEpoch});
    }

    // Low watermark: evict in batches (to maxTiles - maxTiles/8) so a store
    // hovering at the budget does not pay this path on every insert.
    const size_t target = maxTiles - maxTiles / 8;
    if (refs.size() <= target) return;
    const size_t toRemove = refs.size() - target;

    // Order OUTSIDE any lock: stale epochs first, then least-recently-touched.
    const auto victimFirst = [](const Ref &a, const Ref &b) {
        if (a.stale != b.stale) return a.stale > b.stale;
        return a.touched < b.touched;
    };
    std::nth_element(refs.begin(), refs.begin() + static_cast<ptrdiff_t>(toRemove - 1), refs.end(),
                     victimFirst);

    std::unique_lock lock(mutex_);
    for (size_t i = 0; i < toRemove; ++i) map_.erase(refs[i].key);
}

size_t TileStore::size() const
{
    std::shared_lock lock(mutex_);
    return map_.size();
}
}
