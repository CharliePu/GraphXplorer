#ifndef TILECACHE_H
#define TILECACHE_H

#include <cstdint>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "../Math/Interval.h"
#include "../Util/Contracts.h"

namespace gx
{
struct FrameStamp
{
    uint64_t value{0};
    bool operator==(const FrameStamp &) const = default;
};

struct GpuResidency
{
    bool resident{false};
    TextureSlice regionSlice{};
    BufferRange contourRange{};
    size_t gpuBytes{0};
    bool operator==(const GpuResidency &) const = default;
};

enum class TileValueState
{
    Unknown,
    UniformFalse,
    UniformTrue,
    Mixed
};

enum class TileWorkState
{
    Idle,
    IntervalQueued,
    RegionQueued,
    RegionReady,
    ContourQueued,
    ContourReady,
    UploadQueued,
    GpuResident,
    Presented
};

struct TileRecord
{
    TileKey key{};
    FormulaSemanticsHash semanticsHash{};
    TileValueState valueState{TileValueState::Unknown};
    TileWorkState workState{TileWorkState::Idle};
    TileExistenceState existence{TileExistenceState::Unknown};
    std::optional<Interval> interval{};
    std::optional<RegionImageRef> regionPixels{};
    std::optional<ContourSegmentRange> contourSegments{};
    GpuResidency gpuResidency{};
    FrameStamp lastTouched{};
    FrameStamp lastPresented{};
    bool operator==(const TileRecord &) const = default;
};

struct TileApplyResult
{
    size_t applied{0};
    size_t rejected{0};
};

struct TileDebugCounts
{
    size_t intervalQueued{0};
    size_t regionQueued{0};
    size_t stuckIntervalQueued{0};
    size_t stuckRegionQueued{0};
};

struct TileQueuedRecoveryResult
{
    size_t intervalQueued{0};
    size_t regionQueued{0};

    [[nodiscard]] size_t total() const
    {
        return intervalQueued + regionQueued;
    }
};

class TileCache
{
public:
    [[nodiscard]] static bool validTransition(TileStage from, TileStage to);

    TileApplyResult apply(const TileTransaction &transaction);
    bool transition(const TileKey &key, FormulaSemanticsHash semanticsHash, TileStage to);
    bool erase(const TileKey &key, FormulaSemanticsHash semanticsHash);
    [[nodiscard]] TileRecord &getOrCreate(
        const TileKey &key,
        FormulaSemanticsHash semanticsHash);

    [[nodiscard]] const TileRecord *find(const TileKey &key, FormulaSemanticsHash semanticsHash) const;
    [[nodiscard]] const TileRecord *findNearestUniformAncestorOrSelf(
        const TileKey &key,
        FormulaSemanticsHash semanticsHash) const;
    [[nodiscard]] const TileRecord *findNearestRenderableMixedAncestor(
        const TileKey &key,
        FormulaSemanticsHash semanticsHash) const;
    [[nodiscard]] bool hasDescendantRecord(
        const TileKey &key,
        FormulaSemanticsHash semanticsHash) const;
    [[nodiscard]] bool hasRenderableDescendant(
        const TileKey &key,
        FormulaSemanticsHash semanticsHash) const;
    TileApplyResult applyProofTree(TileProofTreePatch patch);
    [[nodiscard]] const TileProofTree *findProofTree(
        const TileKey &rootKey,
        FormulaSemanticsHash semanticsHash) const;
    [[nodiscard]] size_t proofNodeCountForFormula(FormulaSemanticsHash semanticsHash) const;
    [[nodiscard]] std::vector<int> occupiedLevelsForFormula(FormulaSemanticsHash semanticsHash) const;
    [[nodiscard]] std::vector<TileRecord> recordsForFormula(FormulaSemanticsHash semanticsHash) const;
    [[nodiscard]] TileDebugCounts debugCountsForFormula(FormulaSemanticsHash semanticsHash) const;
    TileQueuedRecoveryResult recoverQueuedWork(FormulaSemanticsHash semanticsHash);
    [[nodiscard]] size_t size() const;
    void clear();

private:
    struct XYKey
    {
        int64_t x{0};
        int64_t y{0};
        bool operator==(const XYKey &) const = default;
    };

    struct XYKeyHash
    {
        size_t operator()(const XYKey &key) const noexcept
        {
            auto hash = std::hash<int64_t>{}(key.x);
            hash ^= std::hash<int64_t>{}(key.y) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
            return hash;
        }
    };

    struct LevelBucket
    {
        std::unordered_map<XYKey, TileRecord, XYKeyHash> records;
    };

    struct FormulaTileIndex
    {
        std::unordered_map<int, LevelBucket> levels;
        std::unordered_map<TileKey, TileProofTree, TileKeyHash> proofTrees;
        std::set<int> occupiedLevels;
        size_t recordCount{0};
        size_t proofNodeCount{0};
    };

    [[nodiscard]] static XYKey xyKeyFor(const TileKey &key);
    [[nodiscard]] static TileKey ancestorAtLevel(const TileKey &key, int level);
    [[nodiscard]] static bool renderReadyUniform(const TileRecord &record);
    [[nodiscard]] static TileClassification classificationForValueState(TileValueState valueState);
    [[nodiscard]] static TileValueState valueStateForClassification(TileClassification classification);
    [[nodiscard]] static TileStage stageForRecord(const TileRecord &record);
    static void applyDeltaToRecord(TileRecord &record, const TileDelta &delta);
    static void applyTransitionToRecord(TileRecord &record, TileStage to);
    [[nodiscard]] static const TileRecord *findInIndex(const FormulaTileIndex &index, const TileKey &key);
    [[nodiscard]] static TileRecord *findMutableInIndex(FormulaTileIndex &index, const TileKey &key);
    [[nodiscard]] static const TileRecord *findUniformAncestorInIndex(
        const FormulaTileIndex &index,
        const TileKey &key,
        bool includeSelf);
    [[nodiscard]] static const TileRecord *findRenderableMixedAncestorInIndex(
        const FormulaTileIndex &index,
        const TileKey &key);
    [[nodiscard]] static bool hasDescendantRecordInIndex(
        const FormulaTileIndex &index,
        const TileKey &key);
    static bool eraseProofTree(FormulaTileIndex &index, const TileKey &rootKey);
    static void pruneProofTrees(FormulaTileIndex &index, const TileKey &parent);
    static TileRecord &putRecord(FormulaTileIndex &index, TileRecord record);
    static bool eraseRecord(FormulaTileIndex &index, const TileKey &key);
    static void normalizeUniformAuthority(FormulaTileIndex &index,
                                          const TileKey &key,
                                          FormulaSemanticsHash semanticsHash);
    static bool promoteParentIfChildrenAgree(FormulaTileIndex &index,
                                             const TileKey &parent,
                                             FormulaSemanticsHash semanticsHash);
    static void pruneDescendants(FormulaTileIndex &index, const TileKey &parent);

    std::unordered_map<uint64_t, FormulaTileIndex> formulas;
};
}

#endif // TILECACHE_H
