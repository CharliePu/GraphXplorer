#include "TileCache.h"

#include <algorithm>
#include <tuple>
#include <unordered_set>
#include <utility>

#include "TileMath.h"

namespace gx
{
bool TileCache::validTransition(const TileStage from, const TileStage to)
{
    if (from == to)
    {
        return true;
    }

    switch (from)
    {
    case TileStage::Unknown:
        return to == TileStage::IntervalQueued || to == TileStage::Evicted;
    case TileStage::IntervalQueued:
        return to == TileStage::IntervalReady || to == TileStage::Evicted;
    case TileStage::IntervalReady:
        return to == TileStage::UniformTrue
            || to == TileStage::UniformFalse
            || to == TileStage::MixedNeedsRegion
            || to == TileStage::Evicted;
    case TileStage::UniformTrue:
    case TileStage::UniformFalse:
        return to == TileStage::UploadQueued || to == TileStage::GpuResident || to == TileStage::Evicted;
    case TileStage::MixedNeedsRegion:
        return to == TileStage::RegionQueued || to == TileStage::ContourQueued || to == TileStage::Evicted;
    case TileStage::RegionQueued:
        return to == TileStage::RegionReady || to == TileStage::Evicted;
    case TileStage::RegionReady:
        return to == TileStage::ContourQueued || to == TileStage::UploadQueued || to == TileStage::Evicted;
    case TileStage::ContourQueued:
        return to == TileStage::ContourReady || to == TileStage::Evicted;
    case TileStage::ContourReady:
        return to == TileStage::UploadQueued || to == TileStage::Evicted;
    case TileStage::UploadQueued:
        return to == TileStage::GpuResident || to == TileStage::Evicted;
    case TileStage::GpuResident:
        return to == TileStage::Presented || to == TileStage::Evicted;
    case TileStage::Presented:
        return to == TileStage::GpuResident || to == TileStage::Evicted;
    case TileStage::Evicted:
        return to == TileStage::IntervalQueued;
    }

    return false;
}

TileApplyResult TileCache::apply(const TileTransaction &transaction)
{
    if (!transaction.valid())
    {
        return {.applied = 0, .rejected = transaction.deltas.size()};
    }

    auto stagedRecords = records;
    for (const auto &delta : transaction.deltas)
    {
        CacheKey key{delta.key, delta.semanticsHash};
        auto [it, inserted] = stagedRecords.try_emplace(key);
        auto &record = it->second;

        if (inserted)
        {
            record.key = delta.key;
            record.semanticsHash = delta.semanticsHash;
            record.generation = delta.header.generation;
        }

        if (delta.header.generation < record.generation
            || delta.semanticsHash != record.semanticsHash
            || !validTransition(record.stage, delta.stage))
        {
            return {.applied = 0, .rejected = transaction.deltas.size()};
        }

        record.generation = delta.header.generation;
        record.stage = delta.stage;
        record.classification = delta.classification;
        if (delta.interval)
        {
            record.interval = delta.interval;
        }
        if (delta.region)
        {
            record.regionPixels = delta.region;
        }
        if (delta.contours)
        {
            record.contourSegments = delta.contours;
        }

    }

    records = std::move(stagedRecords);
    return {.applied = transaction.deltas.size(), .rejected = 0};
}

bool TileCache::transition(const TileKey &key,
                           const FormulaSemanticsHash semanticsHash,
                           const uint64_t generation,
                           const TileStage to)
{
    CacheKey cacheKey{key, semanticsHash};
    auto [it, inserted] = records.try_emplace(cacheKey);
    auto &record = it->second;
    if (inserted)
    {
        record.key = key;
        record.semanticsHash = semanticsHash;
        record.generation = generation;
    }

    if (generation < record.generation || !validTransition(record.stage, to))
    {
        return false;
    }

    record.generation = generation;
    record.stage = to;
    return true;
}

const TileRecord *TileCache::find(const TileKey &key, const FormulaSemanticsHash semanticsHash) const
{
    const auto it = records.find(CacheKey{key, semanticsHash});
    if (it == records.end())
    {
        return nullptr;
    }
    return &it->second;
}

std::vector<TileRecord> TileCache::recordsForFormula(const FormulaSemanticsHash semanticsHash) const
{
    std::vector<TileRecord> result;
    for (const auto &[key, record] : records)
    {
        if (key.semanticsHash == semanticsHash)
        {
            result.push_back(record);
        }
    }
    return result;
}

size_t TileCache::size() const
{
    return records.size();
}

void TileCache::clear()
{
    records.clear();
}

TileCoverageIndex::TileCoverageIndex(const TileCache &cache): cache{cache}
{
}

std::vector<TileKey> TileCoverageIndex::visibleCover(const ViewportRequest &request, const int maxCells) const
{
    std::vector<TileKey> keys;
    if (!request.valid() || maxCells <= 0)
    {
        return keys;
    }

    const auto level = targetTileLevel(
        request.xRange,
        request.yRange,
        request.framebufferWidth,
        request.framebufferHeight);
    const auto [minX, maxX] = tileIndexBounds(request.xRange, level);
    const auto [minY, maxY] = tileIndexBounds(request.yRange, level);
    const auto width = maxX - minX + 1;
    const auto height = maxY - minY + 1;
    if (width <= 0 || height <= 0 || width * height > maxCells)
    {
        return keys;
    }

    keys.reserve(static_cast<size_t>(width * height));
    const auto availableRecords = cache.recordsForFormula(request.formula.semanticsHash);
    std::unordered_set<TileKey, TileKeyHash> selected;
    for (auto y = minY; y <= maxY; ++y)
    {
        for (auto x = minX; x <= maxX; ++x)
        {
            TileKey key{x, y, level};
            if (cache.find(key, request.formula.semanticsHash))
            {
                selected.insert(key);
                continue;
            }

            const TileKey targetCell{x, y, level};
            const TileRecord *bestParent = nullptr;
            for (const auto &record : availableRecords)
            {
                if (!parentCoversChild(record.key, targetCell))
                {
                    continue;
                }
                if (!bestParent || record.key.level < bestParent->key.level)
                {
                    bestParent = &record;
                }
            }
            if (bestParent)
            {
                selected.insert(bestParent->key);
            }
        }
    }

    keys.assign(selected.begin(), selected.end());

    std::ranges::sort(keys, [](const TileKey &lhs, const TileKey &rhs)
    {
        return std::tie(lhs.level, lhs.y, lhs.x) < std::tie(rhs.level, rhs.y, rhs.x);
    });
    return keys;
}
}
