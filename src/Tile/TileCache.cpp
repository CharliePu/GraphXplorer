#include "TileCache.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "TileMath.h"
#include "../Util/PerformanceProfiler.h"

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
        return to == TileStage::IntervalReady || to == TileStage::Unknown || to == TileStage::Evicted;
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
        return to == TileStage::RegionReady || to == TileStage::MixedNeedsRegion || to == TileStage::Evicted;
    case TileStage::RegionReady:
        return to == TileStage::RegionQueued
            || to == TileStage::ContourQueued
            || to == TileStage::UploadQueued
            || to == TileStage::Evicted;
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

TileCache::XYKey TileCache::xyKeyFor(const TileKey &key)
{
    return {key.x, key.y};
}

TileKey TileCache::ancestorAtLevel(const TileKey &key, const int level)
{
    if (level <= key.level)
    {
        return key;
    }
    const auto shift = level - key.level;
    return {floorDivByPow2(key.x, shift), floorDivByPow2(key.y, shift), level};
}

bool TileCache::renderReadyUniform(const TileRecord &record)
{
    return record.valueState == TileValueState::UniformTrue
        || record.valueState == TileValueState::UniformFalse;
}

TileClassification TileCache::classificationForValueState(const TileValueState valueState)
{
    switch (valueState)
    {
    case TileValueState::UniformFalse:
        return TileClassification::UniformFalse;
    case TileValueState::UniformTrue:
        return TileClassification::UniformTrue;
    case TileValueState::Mixed:
        return TileClassification::Mixed;
    case TileValueState::Unknown:
    default:
        return TileClassification::Unknown;
    }
}

TileValueState TileCache::valueStateForClassification(const TileClassification classification)
{
    switch (classification)
    {
    case TileClassification::UniformFalse:
        return TileValueState::UniformFalse;
    case TileClassification::UniformTrue:
        return TileValueState::UniformTrue;
    case TileClassification::Mixed:
        return TileValueState::Mixed;
    case TileClassification::Unknown:
    default:
        return TileValueState::Unknown;
    }
}

TileStage TileCache::stageForRecord(const TileRecord &record)
{
    if (record.valueState == TileValueState::UniformTrue)
    {
        if (record.workState == TileWorkState::UploadQueued)
        {
            return TileStage::UploadQueued;
        }
        if (record.workState == TileWorkState::GpuResident)
        {
            return TileStage::GpuResident;
        }
        if (record.workState == TileWorkState::Presented)
        {
            return TileStage::Presented;
        }
        return TileStage::UniformTrue;
    }
    if (record.valueState == TileValueState::UniformFalse)
    {
        if (record.workState == TileWorkState::UploadQueued)
        {
            return TileStage::UploadQueued;
        }
        if (record.workState == TileWorkState::GpuResident)
        {
            return TileStage::GpuResident;
        }
        if (record.workState == TileWorkState::Presented)
        {
            return TileStage::Presented;
        }
        return TileStage::UniformFalse;
    }
    if (record.valueState == TileValueState::Mixed)
    {
        switch (record.workState)
        {
        case TileWorkState::RegionQueued:
            return TileStage::RegionQueued;
        case TileWorkState::RegionReady:
            return TileStage::RegionReady;
        case TileWorkState::ContourQueued:
            return TileStage::ContourQueued;
        case TileWorkState::ContourReady:
            return TileStage::ContourReady;
        case TileWorkState::UploadQueued:
            return TileStage::UploadQueued;
        case TileWorkState::GpuResident:
            return TileStage::GpuResident;
        case TileWorkState::Presented:
            return TileStage::Presented;
        case TileWorkState::Idle:
        case TileWorkState::IntervalQueued:
        default:
            return TileStage::MixedNeedsRegion;
        }
    }

    if (record.workState == TileWorkState::IntervalQueued)
    {
        return TileStage::IntervalQueued;
    }
    return TileStage::Unknown;
}

void TileCache::applyDeltaToRecord(TileRecord &record, const TileDelta &delta)
{
    switch (delta.stage)
    {
    case TileStage::Unknown:
        record.valueState = TileValueState::Unknown;
        record.workState = TileWorkState::Idle;
        record.existence = TileExistenceState::Unknown;
        record.interval.reset();
        record.regionPixels.reset();
        record.contourSegments.reset();
        record.gpuResidency = {};
        break;
    case TileStage::IntervalQueued:
        record.workState = TileWorkState::IntervalQueued;
        break;
    case TileStage::IntervalReady:
        record.valueState = valueStateForClassification(delta.classification);
        record.workState = TileWorkState::Idle;
        if (record.valueState == TileValueState::UniformTrue)
        {
            record.existence = TileExistenceState::Exists;
        }
        else if (record.valueState == TileValueState::UniformFalse)
        {
            record.existence = TileExistenceState::Empty;
        }
        break;
    case TileStage::UniformTrue:
        record.valueState = TileValueState::UniformTrue;
        record.workState = TileWorkState::Idle;
        record.existence = TileExistenceState::Exists;
        break;
    case TileStage::UniformFalse:
        record.valueState = TileValueState::UniformFalse;
        record.workState = TileWorkState::Idle;
        record.existence = TileExistenceState::Empty;
        break;
    case TileStage::MixedNeedsRegion:
        record.valueState = TileValueState::Mixed;
        record.workState = TileWorkState::Idle;
        record.regionPixels.reset();
        record.contourSegments.reset();
        record.gpuResidency = {};
        break;
    case TileStage::RegionQueued:
        record.valueState = TileValueState::Mixed;
        record.workState = TileWorkState::RegionQueued;
        break;
    case TileStage::RegionReady:
        record.valueState = TileValueState::Mixed;
        record.workState = TileWorkState::RegionReady;
        break;
    case TileStage::ContourQueued:
        if (record.valueState == TileValueState::Unknown)
        {
            record.valueState = valueStateForClassification(delta.classification);
        }
        record.workState = TileWorkState::ContourQueued;
        break;
    case TileStage::ContourReady:
        if (record.valueState == TileValueState::Unknown)
        {
            record.valueState = valueStateForClassification(delta.classification);
        }
        record.workState = TileWorkState::ContourReady;
        break;
    case TileStage::UploadQueued:
        if (delta.classification != TileClassification::Unknown)
        {
            record.valueState = valueStateForClassification(delta.classification);
        }
        record.workState = TileWorkState::UploadQueued;
        break;
    case TileStage::GpuResident:
        if (delta.classification != TileClassification::Unknown)
        {
            record.valueState = valueStateForClassification(delta.classification);
        }
        record.workState = TileWorkState::GpuResident;
        break;
    case TileStage::Presented:
        if (delta.classification != TileClassification::Unknown)
        {
            record.valueState = valueStateForClassification(delta.classification);
        }
        record.workState = TileWorkState::Presented;
        break;
    case TileStage::Evicted:
        record.valueState = TileValueState::Unknown;
        record.workState = TileWorkState::Idle;
        record.existence = TileExistenceState::Unknown;
        break;
    }

    if (delta.interval)
    {
        record.interval = delta.interval;
    }
    if (delta.existence)
    {
        record.existence = *delta.existence;
    }
    if (record.valueState == TileValueState::UniformTrue
        || record.valueState == TileValueState::UniformFalse)
    {
        record.regionPixels.reset();
        record.contourSegments.reset();
        record.gpuResidency = {};
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

void TileCache::applyTransitionToRecord(TileRecord &record, const TileStage to)
{
    TileDelta delta;
    delta.header = {};
    delta.semanticsHash = record.semanticsHash;
    delta.key = record.key;
    delta.stage = to;
    delta.classification = classificationForValueState(record.valueState);
    applyDeltaToRecord(record, delta);
}

const TileRecord *TileCache::findInIndex(const FormulaTileIndex &index, const TileKey &key)
{
    const auto levelIt = index.levels.find(key.level);
    if (levelIt == index.levels.end())
    {
        return nullptr;
    }

    const auto recordIt = levelIt->second.records.find(xyKeyFor(key));
    if (recordIt == levelIt->second.records.end())
    {
        return nullptr;
    }
    return &recordIt->second;
}

TileRecord *TileCache::findMutableInIndex(FormulaTileIndex &index, const TileKey &key)
{
    auto levelIt = index.levels.find(key.level);
    if (levelIt == index.levels.end())
    {
        return nullptr;
    }

    auto recordIt = levelIt->second.records.find(xyKeyFor(key));
    if (recordIt == levelIt->second.records.end())
    {
        return nullptr;
    }
    return &recordIt->second;
}

const TileRecord *TileCache::findUniformAncestorInIndex(
    const FormulaTileIndex &index,
    const TileKey &key,
    const bool includeSelf)
{
    for (auto levelIt = index.occupiedLevels.rbegin(); levelIt != index.occupiedLevels.rend(); ++levelIt)
    {
        const auto level = *levelIt;
        if (level < key.level || (!includeSelf && level == key.level))
        {
            continue;
        }

        const auto ancestor = ancestorAtLevel(key, level);
        const auto *record = findInIndex(index, ancestor);
        if (record && renderReadyUniform(*record))
        {
            return record;
        }
    }
    return nullptr;
}

const TileRecord *TileCache::findRenderableMixedAncestorInIndex(
    const FormulaTileIndex &index,
    const TileKey &key)
{
    for (auto levelIt = index.occupiedLevels.upper_bound(key.level);
         levelIt != index.occupiedLevels.end();
         ++levelIt)
    {
        const auto ancestor = ancestorAtLevel(key, *levelIt);
        const auto *record = findInIndex(index, ancestor);
        if (record && record->valueState == TileValueState::Mixed && record->regionPixels)
        {
            return record;
        }
    }
    return nullptr;
}

bool TileCache::hasDescendantRecordInIndex(const FormulaTileIndex &index, const TileKey &key)
{
    for (auto levelIt = index.occupiedLevels.begin();
         levelIt != index.occupiedLevels.end() && *levelIt < key.level;
         ++levelIt)
    {
        const auto bucketIt = index.levels.find(*levelIt);
        if (bucketIt == index.levels.end())
        {
            continue;
        }

        for (const auto &[xy, record] : bucketIt->second.records)
        {
            (void)xy;
            if (parentCoversChild(key, record.key))
            {
                return true;
            }
        }
    }
    return false;
}

bool TileCache::eraseProofTree(FormulaTileIndex &index, const TileKey &rootKey)
{
    const auto it = index.proofTrees.find(rootKey);
    if (it == index.proofTrees.end())
    {
        return false;
    }

    index.proofNodeCount -= it->second.nodes.size();
    index.proofTrees.erase(it);
    return true;
}

void TileCache::pruneProofTrees(FormulaTileIndex &index, const TileKey &parent)
{
    std::vector<TileKey> toErase;
    for (const auto &[rootKey, tree] : index.proofTrees)
    {
        (void)tree;
        if (parentCoversChild(parent, rootKey))
        {
            toErase.push_back(rootKey);
        }
    }

    for (const auto &rootKey : toErase)
    {
        eraseProofTree(index, rootKey);
    }
}

TileRecord &TileCache::putRecord(FormulaTileIndex &index, TileRecord record)
{
    auto &bucket = index.levels[record.key.level];
    const auto [it, inserted] = bucket.records.insert_or_assign(xyKeyFor(record.key), std::move(record));
    if (inserted)
    {
        ++index.recordCount;
    }
    index.occupiedLevels.insert(it->second.key.level);
    return it->second;
}

bool TileCache::eraseRecord(FormulaTileIndex &index, const TileKey &key)
{
    auto levelIt = index.levels.find(key.level);
    if (levelIt == index.levels.end())
    {
        eraseProofTree(index, key);
        return false;
    }

    const auto erased = levelIt->second.records.erase(xyKeyFor(key)) > 0;
    if (!erased)
    {
        eraseProofTree(index, key);
        return false;
    }

    eraseProofTree(index, key);
    --index.recordCount;
    if (levelIt->second.records.empty())
    {
        index.levels.erase(levelIt);
        index.occupiedLevels.erase(key.level);
    }
    return true;
}

void TileCache::pruneDescendants(FormulaTileIndex &index, const TileKey &parent)
{
    eraseProofTree(index, parent);
    pruneProofTrees(index, parent);

    std::vector<TileKey> toErase;
    for (auto levelIt = index.occupiedLevels.begin();
         levelIt != index.occupiedLevels.end() && *levelIt < parent.level;
         ++levelIt)
    {
        const auto bucketIt = index.levels.find(*levelIt);
        if (bucketIt == index.levels.end())
        {
            continue;
        }

        for (const auto &[xy, record] : bucketIt->second.records)
        {
            (void)xy;
            if (parentCoversChild(parent, record.key))
            {
                toErase.push_back(record.key);
            }
        }
    }

    for (const auto &key : toErase)
    {
        eraseRecord(index, key);
    }
}

bool TileCache::promoteParentIfChildrenAgree(
    FormulaTileIndex &index,
    const TileKey &parent,
    const FormulaSemanticsHash semanticsHash)
{
    std::optional<TileValueState> valueState;
    for (const auto &child : tileChildren(parent))
    {
        const auto *childRecord = findInIndex(index, child);
        if (!childRecord || !renderReadyUniform(*childRecord))
        {
            return false;
        }

        if (!valueState)
        {
            valueState = childRecord->valueState;
        }
        else if (*valueState != childRecord->valueState)
        {
            return false;
        }
    }

    if (!valueState)
    {
        return false;
    }

    const auto interval = *valueState == TileValueState::UniformTrue
        ? Interval{1.0, 1.0}
        : Interval{0.0, 0.0};

    TileRecord parentRecord;
    if (const auto *existing = findInIndex(index, parent))
    {
        parentRecord = *existing;
    }
    parentRecord.key = parent;
    parentRecord.semanticsHash = semanticsHash;
    parentRecord.valueState = *valueState;
    parentRecord.workState = TileWorkState::Idle;
    parentRecord.existence = *valueState == TileValueState::UniformTrue
        ? TileExistenceState::Exists
        : TileExistenceState::Empty;
    parentRecord.interval = interval;
    parentRecord.regionPixels.reset();
    parentRecord.contourSegments.reset();
    parentRecord.gpuResidency = {};
    putRecord(index, std::move(parentRecord));
    return true;
}

void TileCache::normalizeUniformAuthority(
    FormulaTileIndex &index,
    const TileKey &key,
    const FormulaSemanticsHash semanticsHash)
{
    if (const auto *ancestor = findUniformAncestorInIndex(index, key, false))
    {
        pruneDescendants(index, ancestor->key);
        return;
    }

    auto current = key;
    while (true)
    {
        if (const auto *record = findInIndex(index, current); record && renderReadyUniform(*record))
        {
            pruneDescendants(index, current);
        }

        if (current.level == std::numeric_limits<int>::max())
        {
            return;
        }

        const auto parent = tileParent(current);
        if (!promoteParentIfChildrenAgree(index, parent, semanticsHash))
        {
            return;
        }

        pruneDescendants(index, parent);
        current = parent;
    }
}

TileApplyResult TileCache::apply(const TileTransaction &transaction)
{
    GRAPHX_PROFILE_SCOPE("tileCache.apply");
    if (!transaction.valid())
    {
        return {.applied = 0, .rejected = transaction.deltas.size()};
    }

    const FormulaTileIndex emptyIndex;
    const auto formulaIt = formulas.find(transaction.semanticsHash.value);
    const auto &baseIndex = formulaIt != formulas.end()
        ? formulaIt->second
        : emptyIndex;
    std::unordered_map<TileKey, std::optional<TileRecord>, TileKeyHash> pending;
    std::vector<TileKey> touchedKeys;
    touchedKeys.reserve(transaction.deltas.size());
    size_t applied = 0;
    size_t rejected = 0;

    const auto findPendingRecord = [&pending](const TileKey &key) -> const std::optional<TileRecord> *
    {
        const auto it = pending.find(key);
        if (it == pending.end())
        {
            return nullptr;
        }
        return &it->second;
    };

    const auto findOverlayRecord = [&](const TileKey &key) -> const TileRecord *
    {
        if (const auto *pendingRecord = findPendingRecord(key))
        {
            return pendingRecord->has_value() ? &**pendingRecord : nullptr;
        }
        return findInIndex(baseIndex, key);
    };

    const auto findUniformAncestor = [&](const TileKey &key, const bool includeSelf) -> const TileRecord *
    {
        const TileRecord *best = nullptr;
        auto bestLevel = std::numeric_limits<int>::min();
        const auto consider = [&](const TileRecord *record)
        {
            if (!record || !renderReadyUniform(*record))
            {
                return;
            }
            const auto sameTile = record->key == key;
            const auto covers = sameTile || parentCoversChild(record->key, key);
            if (covers
                && (includeSelf || !sameTile)
                && record->key.level > bestLevel)
            {
                best = record;
                bestLevel = record->key.level;
            }
        };

        for (const auto &[candidateKey, record] : pending)
        {
            (void)candidateKey;
            if (record)
            {
                consider(&*record);
            }
        }

        for (auto levelIt = baseIndex.occupiedLevels.lower_bound(key.level);
             levelIt != baseIndex.occupiedLevels.end();
             ++levelIt)
        {
            if (!includeSelf && *levelIt == key.level)
            {
                continue;
            }

            consider(findInIndex(baseIndex, ancestorAtLevel(key, *levelIt)));
        }
        return best;
    };

    const auto markErase = [&pending](const TileKey &key)
    {
        pending.insert_or_assign(key, std::nullopt);
    };

    const auto pruneDescendants = [&](const TileKey &parent)
    {
        std::vector<TileKey> toErase;
        for (const auto &[key, record] : pending)
        {
            (void)record;
            if (parentCoversChild(parent, key))
            {
                toErase.push_back(key);
            }
        }

        for (auto levelIt = baseIndex.occupiedLevels.begin();
             levelIt != baseIndex.occupiedLevels.end() && *levelIt < parent.level;
             ++levelIt)
        {
            const auto bucketIt = baseIndex.levels.find(*levelIt);
            if (bucketIt == baseIndex.levels.end())
            {
                continue;
            }

            for (const auto &[xy, record] : bucketIt->second.records)
            {
                (void)xy;
                if (parentCoversChild(parent, record.key))
                {
                    toErase.push_back(record.key);
                }
            }
        }

        for (const auto &key : toErase)
        {
            markErase(key);
        }
    };

    for (const auto &delta : transaction.deltas)
    {
        if (const auto *ancestor = findUniformAncestor(delta.key, false))
        {
            pruneDescendants(ancestor->key);
            ++rejected;
            continue;
        }

        TileRecord record;
        if (const auto *existing = findOverlayRecord(delta.key))
        {
            record = *existing;
        }
        else
        {
            record.key = delta.key;
            record.semanticsHash = delta.semanticsHash;
        }

        if (delta.semanticsHash != record.semanticsHash
            || !validTransition(stageForRecord(record), delta.stage))
        {
            return {.applied = 0, .rejected = transaction.deltas.size()};
        }

        if (delta.stage == TileStage::Evicted)
        {
            markErase(delta.key);
            touchedKeys.push_back(delta.key);
            ++applied;
            continue;
        }

        applyDeltaToRecord(record, delta);

        pending.insert_or_assign(delta.key, std::move(record));
        touchedKeys.push_back(delta.key);
        ++applied;
    }

    auto &index = formulas[transaction.semanticsHash.value];
    for (auto &[key, record] : pending)
    {
        if (record)
        {
            putRecord(index, std::move(*record));
        }
        else
        {
            eraseRecord(index, key);
        }
    }

    for (const auto &key : touchedKeys)
    {
        normalizeUniformAuthority(index, key, transaction.semanticsHash);
    }

    if (index.recordCount == 0)
    {
        formulas.erase(transaction.semanticsHash.value);
    }
    return {.applied = applied, .rejected = rejected};
}

bool TileCache::transition(const TileKey &key,
                           const FormulaSemanticsHash semanticsHash,
                           const TileStage to)
{
    auto &index = formulas[semanticsHash.value];
    if (const auto *ancestor = findUniformAncestorInIndex(index, key, false))
    {
        pruneDescendants(index, ancestor->key);
        return false;
    }

    TileRecord record;
    if (const auto *existing = findInIndex(index, key))
    {
        record = *existing;
    }
    else
    {
        record.key = key;
        record.semanticsHash = semanticsHash;
    }

    if (!validTransition(stageForRecord(record), to))
    {
        return false;
    }

    if (to == TileStage::Evicted)
    {
        eraseRecord(index, key);
        if (index.recordCount == 0)
        {
            formulas.erase(semanticsHash.value);
        }
        return true;
    }

    applyTransitionToRecord(record, to);
    putRecord(index, std::move(record));
    normalizeUniformAuthority(index, key, semanticsHash);
    return true;
}

bool TileCache::erase(const TileKey &key, const FormulaSemanticsHash semanticsHash)
{
    auto formulaIt = formulas.find(semanticsHash.value);
    if (formulaIt == formulas.end())
    {
        return false;
    }

    const auto erased = eraseRecord(formulaIt->second, key);
    if (formulaIt->second.recordCount == 0)
    {
        formulas.erase(formulaIt);
    }
    return erased;
}

TileRecord &TileCache::getOrCreate(
    const TileKey &key,
    const FormulaSemanticsHash semanticsHash)
{
    auto &index = formulas[semanticsHash.value];
    if (auto *existing = findMutableInIndex(index, key))
    {
        return *existing;
    }

    TileRecord record;
    record.key = key;
    record.semanticsHash = semanticsHash;
    return putRecord(index, std::move(record));
}

const TileRecord *TileCache::find(const TileKey &key, const FormulaSemanticsHash semanticsHash) const
{
    const auto formulaIt = formulas.find(semanticsHash.value);
    if (formulaIt == formulas.end())
    {
        return nullptr;
    }
    return findInIndex(formulaIt->second, key);
}

const TileRecord *TileCache::findNearestUniformAncestorOrSelf(
    const TileKey &key,
    const FormulaSemanticsHash semanticsHash) const
{
    const auto formulaIt = formulas.find(semanticsHash.value);
    if (formulaIt == formulas.end())
    {
        return nullptr;
    }
    return findUniformAncestorInIndex(formulaIt->second, key, true);
}

const TileRecord *TileCache::findNearestRenderableMixedAncestor(
    const TileKey &key,
    const FormulaSemanticsHash semanticsHash) const
{
    const auto formulaIt = formulas.find(semanticsHash.value);
    if (formulaIt == formulas.end())
    {
        return nullptr;
    }
    return findRenderableMixedAncestorInIndex(formulaIt->second, key);
}

bool TileCache::hasDescendantRecord(const TileKey &key, const FormulaSemanticsHash semanticsHash) const
{
    const auto formulaIt = formulas.find(semanticsHash.value);
    if (formulaIt == formulas.end())
    {
        return false;
    }
    return hasDescendantRecordInIndex(formulaIt->second, key);
}

TileApplyResult TileCache::applyProofTree(TileProofTreePatch patch)
{
    if (!patch.valid())
    {
        return {.applied = 0, .rejected = 1};
    }

    auto formulaIt = formulas.find(patch.semanticsHash.value);
    if (formulaIt == formulas.end())
    {
        return {.applied = 0, .rejected = 1};
    }

    auto &index = formulaIt->second;
    auto *rootRecord = findMutableInIndex(index, patch.tree.rootKey);
    if (!rootRecord || rootRecord->valueState != TileValueState::Mixed)
    {
        return {.applied = 0, .rejected = 1};
    }

    if (patch.tree.existence != TileExistenceState::Unknown)
    {
        rootRecord->existence = patch.tree.existence;
    }
    patch.tree.existence = rootRecord->existence;
    if (rootRecord->regionPixels)
    {
        patch.tree.certainty = rootRecord->regionPixels->certainty;
    }

    eraseProofTree(index, patch.tree.rootKey);
    index.proofNodeCount += patch.tree.nodes.size();
    index.proofTrees.insert_or_assign(patch.tree.rootKey, std::move(patch.tree));
    return {.applied = 1, .rejected = 0};
}

const TileProofTree *TileCache::findProofTree(
    const TileKey &rootKey,
    const FormulaSemanticsHash semanticsHash) const
{
    const auto formulaIt = formulas.find(semanticsHash.value);
    if (formulaIt == formulas.end())
    {
        return nullptr;
    }

    const auto treeIt = formulaIt->second.proofTrees.find(rootKey);
    return treeIt == formulaIt->second.proofTrees.end()
        ? nullptr
        : &treeIt->second;
}

size_t TileCache::proofNodeCountForFormula(const FormulaSemanticsHash semanticsHash) const
{
    const auto formulaIt = formulas.find(semanticsHash.value);
    return formulaIt == formulas.end() ? size_t{0} : formulaIt->second.proofNodeCount;
}

std::vector<int> TileCache::occupiedLevelsForFormula(const FormulaSemanticsHash semanticsHash) const
{
    const auto formulaIt = formulas.find(semanticsHash.value);
    if (formulaIt == formulas.end())
    {
        return {};
    }
    return {formulaIt->second.occupiedLevels.begin(), formulaIt->second.occupiedLevels.end()};
}

std::vector<TileRecord> TileCache::recordsForFormula(const FormulaSemanticsHash semanticsHash) const
{
    std::vector<TileRecord> result;
    const auto formulaIt = formulas.find(semanticsHash.value);
    if (formulaIt == formulas.end())
    {
        return result;
    }

    result.reserve(formulaIt->second.recordCount);
    for (const auto &[level, bucket] : formulaIt->second.levels)
    {
        (void)level;
        for (const auto &[xy, record] : bucket.records)
        {
            (void)xy;
            result.push_back(record);
        }
    }
    return result;
}

TileDebugCounts TileCache::debugCountsForFormula(const FormulaSemanticsHash semanticsHash) const
{
    TileDebugCounts counts;
    const auto formulaIt = formulas.find(semanticsHash.value);
    if (formulaIt == formulas.end())
    {
        return counts;
    }

    for (const auto &[level, bucket] : formulaIt->second.levels)
    {
        (void)level;
        for (const auto &[xy, record] : bucket.records)
        {
            (void)xy;
            if (record.workState == TileWorkState::IntervalQueued)
            {
                ++counts.intervalQueued;
            }
            else if (record.workState == TileWorkState::RegionQueued)
            {
                ++counts.regionQueued;
            }
        }
    }
    return counts;
}

TileQueuedRecoveryResult TileCache::recoverQueuedWork(const FormulaSemanticsHash semanticsHash)
{
    TileQueuedRecoveryResult result;
    const auto formulaIt = formulas.find(semanticsHash.value);
    if (formulaIt == formulas.end())
    {
        return result;
    }

    for (auto &[level, bucket] : formulaIt->second.levels)
    {
        (void)level;
        for (auto &[xy, record] : bucket.records)
        {
            (void)xy;
            if (record.workState == TileWorkState::IntervalQueued)
            {
                applyTransitionToRecord(record, TileStage::Unknown);
                ++result.intervalQueued;
            }
            else if (record.workState == TileWorkState::RegionQueued)
            {
                applyTransitionToRecord(
                    record,
                    record.interval ? TileStage::MixedNeedsRegion : TileStage::Unknown);
                ++result.regionQueued;
            }
        }
    }
    return result;
}

size_t TileCache::size() const
{
    size_t total = 0;
    for (const auto &[semantics, index] : formulas)
    {
        (void)semantics;
        total += index.recordCount;
    }
    return total;
}

void TileCache::clear()
{
    formulas.clear();
}
}
