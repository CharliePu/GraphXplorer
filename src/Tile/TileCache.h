#ifndef TILECACHE_H
#define TILECACHE_H

#include <cstdint>
#include <optional>
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

struct TileRecord
{
    TileKey key{};
    FormulaSemanticsHash semanticsHash{};
    uint64_t generation{0};
    TileStage stage{TileStage::Unknown};
    TileClassification classification{TileClassification::Unknown};
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

class TileCache
{
public:
    [[nodiscard]] static bool validTransition(TileStage from, TileStage to);

    TileApplyResult apply(const TileTransaction &transaction);
    bool transition(const TileKey &key, FormulaSemanticsHash semanticsHash, uint64_t generation, TileStage to);

    [[nodiscard]] const TileRecord *find(const TileKey &key, FormulaSemanticsHash semanticsHash) const;
    [[nodiscard]] std::vector<TileRecord> recordsForFormula(FormulaSemanticsHash semanticsHash) const;
    [[nodiscard]] size_t size() const;
    void clear();

private:
    struct CacheKey
    {
        TileKey tile{};
        FormulaSemanticsHash semanticsHash{};
        bool operator==(const CacheKey &) const = default;
    };

    struct CacheKeyHash
    {
        size_t operator()(const CacheKey &key) const noexcept
        {
            auto hash = TileKeyHash{}(key.tile);
            hash ^= std::hash<uint64_t>{}(key.semanticsHash.value)
                + 0x9e3779b97f4a7c15ull
                + (hash << 6)
                + (hash >> 2);
            return hash;
        }
    };

    std::unordered_map<CacheKey, TileRecord, CacheKeyHash> records;
};

class TileCoverageIndex
{
public:
    explicit TileCoverageIndex(const TileCache &cache);

    [[nodiscard]] std::vector<TileKey> visibleCover(
        const ViewportRequest &request,
        int maxCells = 16384) const;

private:
    const TileCache &cache;
};
}

#endif // TILECACHE_H
