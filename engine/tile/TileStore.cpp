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
            return it->second->state.load(std::memory_order_acquire);
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
    return it->second->state.load(std::memory_order_acquire);
}

void TileStore::publish(const TileKey &key, CoverageTilePtr cov, bool done)
{
    std::shared_lock lock(mutex_);
    const auto it = map_.find(key);
    if (it == map_.end()) return; // slot evicted underneath us; drop
    it->second->snap.store(std::move(cov), std::memory_order_release);
    it->second->state.store(done ? TileState::Done : TileState::Coarse, std::memory_order_release);
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

void TileStore::touch(const TileKey &key, uint64_t frame)
{
    std::shared_lock lock(mutex_);
    const auto it = map_.find(key);
    if (it != map_.end()) it->second->lastTouched.store(frame, std::memory_order_relaxed);
}

void TileStore::evictToBudget(size_t maxTiles, uint64_t keepEpoch)
{
    std::unique_lock lock(mutex_);
    if (map_.size() <= maxTiles) return;

    struct Ref
    {
        TileKey key;
        uint64_t touched;
        bool stale;
    };
    std::vector<Ref> refs;
    refs.reserve(map_.size());
    for (const auto &[k, slot] : map_)
    {
        refs.push_back({k, slot->lastTouched.load(std::memory_order_relaxed), k.epoch < keepEpoch});
    }
    // evict stale epochs first, then least-recently-touched
    std::sort(refs.begin(), refs.end(), [](const Ref &a, const Ref &b) {
        if (a.stale != b.stale) return a.stale > b.stale; // stale first
        return a.touched < b.touched;                     // older first
    });
    size_t toRemove = map_.size() - maxTiles;
    for (size_t i = 0; i < refs.size() && toRemove > 0; ++i)
    {
        map_.erase(refs[i].key);
        --toRemove;
    }
}

size_t TileStore::size() const
{
    std::shared_lock lock(mutex_);
    return map_.size();
}
}
