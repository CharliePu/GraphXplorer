#ifndef VISUALCOVERBUILDER_H
#define VISUALCOVERBUILDER_H

#include <functional>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

#include "../Tile/TileCache.h"
#include "../Tile/TileMath.h"
#include "../Util/Contracts.h"

namespace gx
{
struct CommittedVisualFrame
{
    FormulaSemanticsHash semantics{};
    ViewportRequest viewport{};
    std::vector<DisplayTile> tiles{};
};

struct VisualFrame
{
    std::vector<DisplayTile> tiles{};
    std::vector<DisplayTile> preloadTiles{};
};

class VisualCoverBuilder
{
public:
    using RegionPresentablePredicate = std::function<bool(const RegionImageRef &)>;

    [[nodiscard]] VisualFrame build(const ViewportRequest &request,
                                    const TileCache &tileCache,
                                    const CommittedVisualFrame *previous = nullptr,
                                    int maxSeedCells = 4,
                                    int refinementDepth = DefaultRefinementDepth,
                                    RegionPresentablePredicate regionPresentable = {}) const;

private:
    struct BuildState
    {
        VisualFrame frame{};
        std::unordered_set<TileKey, TileKeyHash> emittedUniformAuthorities{};
        std::unordered_set<uint64_t> preloadedRegions{};
        RegionPresentablePredicate regionPresentable{};
        const CommittedVisualFrame *previous{nullptr};
    };

    static void visit(const ViewportRequest &request,
                      const TileCache &tileCache,
                      const CommittedVisualFrame *previous,
                      const TileKey &key,
                      int leafLevel,
                      BuildState &state);
    static void emitFallbackCell(const ViewportRequest &request,
                                 const TileCache &tileCache,
                                 const TileKey &key,
                                 BuildState &state);
    static void queuePreloadTile(const ViewportRequest &request,
                                 const TileKey &displayKey,
                                 const TileRecord &record,
                                 BuildState &state);
    static void emitUniformAuthority(const ViewportRequest &request,
                                     const TileRecord &record,
                                     BuildState &state);
    [[nodiscard]] static bool mixedRegionPresentable(
        const TileRecord &record,
        const BuildState &state);
    [[nodiscard]] static bool shouldSplitForPartialCover(
        const TileCache &tileCache,
        const CommittedVisualFrame *previous,
        const TileKey &key,
        FormulaSemanticsHash semanticsHash);
    [[nodiscard]] static bool hasPreviousVisualDescendant(
        const TileKey &key,
        const CommittedVisualFrame *previous);
    [[nodiscard]] static std::optional<DisplayTile> currentReadyTileFor(
        const ViewportRequest &request,
        const TileKey &displayKey,
        const TileRecord &record,
        bool fallback,
        const BuildState &state);
    [[nodiscard]] static std::optional<DisplayTile> previousVisualTileFor(
        const ViewportRequest &request,
        const TileKey &displayKey,
        const CommittedVisualFrame *previous);
    [[nodiscard]] static std::optional<DisplayTile> mixedAncestorFallbackTile(
        const ViewportRequest &request,
        const TileKey &displayKey,
        const TileRecord &record,
        BuildState &state);
    [[nodiscard]] static DisplayTile missingTileFor(
        const ViewportRequest &request,
        const TileKey &key);
};
}

#endif // VISUALCOVERBUILDER_H
