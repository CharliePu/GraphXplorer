#ifndef CONTRACTS_H
#define CONTRACTS_H

#include <cstdint>
#include <array>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../Math/Interval.h"

namespace gx
{
inline constexpr uint32_t ContractSchemaVersion = 1;

struct FormulaSourceHash
{
    uint64_t value{0};
    bool operator==(const FormulaSourceHash &) const = default;
};

struct FormulaSemanticsHash
{
    uint64_t value{0};
    bool operator==(const FormulaSemanticsHash &) const = default;
};

struct FormulaBackendHash
{
    uint64_t value{0};
    bool operator==(const FormulaBackendHash &) const = default;
};

struct FormulaTraits
{
    bool hasComparison{false};
    bool hasEquality{false};
    bool supportsRegion{true};
    bool supportsContour{false};
    bool operator==(const FormulaTraits &) const = default;
};

struct ContractHeader
{
    uint32_t schemaVersion{ContractSchemaVersion};
    uint64_t requestId{0};
    uint64_t generation{0};

    [[nodiscard]] bool valid() const
    {
        return schemaVersion == ContractSchemaVersion;
    }

    bool operator==(const ContractHeader &) const = default;
};

struct TileKey
{
    int64_t x{0};
    int64_t y{0};
    int level{0};
    bool operator==(const TileKey &) const = default;
};

struct TileKeyHash
{
    size_t operator()(const TileKey &key) const noexcept
    {
        auto hash = std::hash<int64_t>{}(key.x);
        hash ^= std::hash<int64_t>{}(key.y) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>{}(key.level) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        return hash;
    }
};

struct Rect
{
    double xMin{0.0};
    double xMax{0.0};
    double yMin{0.0};
    double yMax{0.0};
    bool operator==(const Rect &) const = default;
};

struct CompiledFormulaHandle
{
    uint64_t id{0};
    uint64_t version{0};
    FormulaSourceHash sourceHash{};
    FormulaSemanticsHash semanticsHash{};
    FormulaBackendHash backendHash{};
    FormulaTraits traits{};

    [[nodiscard]] bool valid() const
    {
        return id != 0 && version != 0 && semanticsHash.value != 0;
    }

    bool operator==(const CompiledFormulaHandle &) const = default;
};

struct ViewportRequest
{
    ContractHeader header{};
    CompiledFormulaHandle formula{};
    Interval xRange{};
    Interval yRange{};
    int framebufferWidth{0};
    int framebufferHeight{0};
    double devicePixelRatio{1.0};

    [[nodiscard]] bool valid() const
    {
        return header.valid()
            && formula.valid()
            && framebufferWidth > 0
            && framebufferHeight > 0
            && devicePixelRatio > 0.0
            && xRange.upper > xRange.lower
            && yRange.upper > yRange.lower;
    }

    bool operator==(const ViewportRequest &) const = default;
};

enum class TileStage
{
    Unknown,
    IntervalQueued,
    IntervalReady,
    UniformTrue,
    UniformFalse,
    MixedNeedsRegion,
    RegionQueued,
    RegionReady,
    ContourQueued,
    ContourReady,
    UploadQueued,
    GpuResident,
    Presented,
    Evicted
};

enum class TileClassification
{
    Unknown,
    UniformFalse,
    UniformTrue,
    Mixed
};

struct RegionImageRef
{
    uint64_t id{0};
    int width{0};
    int height{0};
    bool operator==(const RegionImageRef &) const = default;
};

struct ContourSegmentRange
{
    uint64_t bufferId{0};
    uint32_t offset{0};
    uint32_t count{0};
    bool operator==(const ContourSegmentRange &) const = default;
};

struct TextureSlice
{
    uint64_t textureId{0};
    uint32_t slice{0};
    bool operator==(const TextureSlice &) const = default;
};

struct BufferRange
{
    uint64_t bufferId{0};
    uint32_t offset{0};
    uint32_t count{0};
    bool operator==(const BufferRange &) const = default;
};

struct TileDelta
{
    ContractHeader header{};
    FormulaSemanticsHash semanticsHash{};
    TileKey key{};
    TileStage stage{TileStage::Unknown};
    TileClassification classification{TileClassification::Unknown};
    std::optional<Interval> interval{};
    std::optional<RegionImageRef> region{};
    std::optional<ContourSegmentRange> contours{};

    [[nodiscard]] bool valid() const
    {
        if (!header.valid() || semanticsHash.value == 0)
        {
            return false;
        }

        switch (stage)
        {
        case TileStage::IntervalReady:
        case TileStage::UniformTrue:
        case TileStage::UniformFalse:
        case TileStage::MixedNeedsRegion:
            return interval.has_value();
        case TileStage::RegionReady:
            return region.has_value();
        case TileStage::ContourReady:
            return contours.has_value();
        default:
            return true;
        }
    }

    bool operator==(const TileDelta &) const = default;
};

struct TileTransaction
{
    ContractHeader header{};
    FormulaSemanticsHash semanticsHash{};
    std::vector<TileDelta> deltas{};

    [[nodiscard]] bool valid() const
    {
        if (!header.valid() || semanticsHash.value == 0)
        {
            return false;
        }
        for (const auto &delta : deltas)
        {
            if (!delta.valid()
                || delta.header.requestId != header.requestId
                || delta.header.generation != header.generation
                || delta.semanticsHash != semanticsHash)
            {
                return false;
            }
        }
        return true;
    }

    bool operator==(const TileTransaction &) const = default;
};

enum class TileVisualState
{
    Missing,
    UniformFalse,
    UniformTrue,
    MixedRegion,
    ContourOnly,
    DebugOverlay,
    DebugUniform,
    DebugMixed,
    DebugMissing
};

struct RenderTileInstance
{
    TileKey key{};
    Rect worldBounds{};
    TileVisualState visualState{TileVisualState::Missing};
    TextureSlice regionSlice{};
    BufferRange contourRange{};
    std::array<float, 4> uvRect{0.0f, 0.0f, 1.0f, 1.0f};
    bool operator==(const RenderTileInstance &) const = default;
};

struct DisplayTile
{
    TileKey desiredKey{};
    TileKey sourceKey{};
    Rect worldBounds{};
    TileVisualState visualState{TileVisualState::Missing};
    std::optional<RegionImageRef> cpuRegion{};
    TextureSlice gpuSlice{};
    std::array<float, 4> uvRect{0.0f, 0.0f, 1.0f, 1.0f};
    bool isFallback{false};
    bool clippedFallback{false};
    bool operator==(const DisplayTile &) const = default;
};

enum class RenderLayer
{
    Background,
    Plot,
    Grid,
    Contour,
    Text,
    Overlay
};

struct PipelineHandle
{
    uint64_t id{0};
    bool operator==(const PipelineHandle &) const = default;
};

struct GeometryHandle
{
    uint64_t id{0};
    bool operator==(const GeometryHandle &) const = default;
};

struct MaterialHandle
{
    uint64_t id{0};
    bool operator==(const MaterialHandle &) const = default;
};

struct TextureSetHandle
{
    uint64_t id{0};
    bool operator==(const TextureSetHandle &) const = default;
};

struct DrawCommand
{
    RenderLayer layer{RenderLayer::Plot};
    PipelineHandle pipeline{};
    GeometryHandle geometry{};
    MaterialHandle material{};
    TextureSetHandle textures{};
    BufferRange instanceRange{};
    uint32_t sortKey{0};
    bool operator==(const DrawCommand &) const = default;
};

struct UploadBudget
{
    size_t maxTextureBytesPerFrame{8 * 1024 * 1024};
    size_t maxBufferBytesPerFrame{2 * 1024 * 1024};
    int maxTextureSlicesPerFrame{64};
    int maxTileInstanceUpdatesPerFrame{4096};
    bool operator==(const UploadBudget &) const = default;
};

struct RenderProgress
{
    size_t regionUploadsThisFrame{0};
    size_t regionUploadBytesThisFrame{0};
    size_t pendingRegionUploadsAfterFrame{0};
    bool regionUploadStateObserved{false};

    [[nodiscard]] bool needsFollowupFrame() const
    {
        return regionUploadsThisFrame > 0 || pendingRegionUploadsAfterFrame > 0;
    }

    void merge(const RenderProgress &next)
    {
        regionUploadsThisFrame += next.regionUploadsThisFrame;
        regionUploadBytesThisFrame += next.regionUploadBytesThisFrame;
        if (next.regionUploadStateObserved)
        {
            pendingRegionUploadsAfterFrame = next.pendingRegionUploadsAfterFrame;
            regionUploadStateObserved = true;
        }
    }

    bool operator==(const RenderProgress &) const = default;
};

struct UploadPlan
{
    std::vector<TileKey> textureUploads{};
    std::vector<TileKey> bufferUploads{};
    size_t textureBytes{0};
    size_t bufferBytes{0};
    bool budgetExhausted{false};
    bool operator==(const UploadPlan &) const = default;
};

struct FrameSnapshot
{
    uint64_t frameId{0};
    std::string appStateDiff{};
    std::optional<ViewportRequest> viewportRequest{};
    std::optional<CompiledFormulaHandle> formula{};
    std::string schedulerSummary{};
    std::vector<TileTransaction> appliedTransactions{};
    std::vector<TileKey> visibleCover{};
    std::vector<DisplayTile> displayTiles{};
    UploadPlan uploadPlan{};
    std::vector<DrawCommand> drawCommands{};
    std::string counters{};
};

[[nodiscard]] inline std::string toDebugString(const TileKey &key)
{
    std::ostringstream out;
    out << "(" << key.x << "," << key.y << "," << key.level << ")";
    return out.str();
}

[[nodiscard]] inline std::string toDebugString(const TileDelta &delta)
{
    std::ostringstream out;
    out << "{schema:" << delta.header.schemaVersion
        << ",request:" << delta.header.requestId
        << ",generation:" << delta.header.generation
        << ",formula:" << delta.semanticsHash.value
        << ",tile:\"" << toDebugString(delta.key) << "\""
        << ",stage:" << static_cast<int>(delta.stage)
        << ",classification:" << static_cast<int>(delta.classification)
        << "}";
    return out.str();
}

[[nodiscard]] inline std::string toJsonSnapshot(const TileTransaction &tx)
{
    std::ostringstream out;
    out << "{\"schemaVersion\":" << tx.header.schemaVersion
        << ",\"requestId\":" << tx.header.requestId
        << ",\"generation\":" << tx.header.generation
        << ",\"semanticsHash\":" << tx.semanticsHash.value
        << ",\"deltas\":[";
    for (size_t i = 0; i < tx.deltas.size(); ++i)
    {
        if (i > 0)
        {
            out << ",";
        }
        const auto &delta = tx.deltas[i];
        out << "{\"tile\":\"" << toDebugString(delta.key)
            << "\",\"stage\":" << static_cast<int>(delta.stage)
            << ",\"classification\":" << static_cast<int>(delta.classification)
            << "}";
    }
    out << "]}";
    return out.str();
}
}

#endif // CONTRACTS_H
