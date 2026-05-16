#ifndef VISUALCOVERBUILDER_H
#define VISUALCOVERBUILDER_H

#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

#include "../Tile/TileCache.h"
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
};

class VisualCoverBuilder
{
public:
    [[nodiscard]] VisualFrame build(const ViewportRequest &request,
                                    const TileCache &tileCache,
                                    const CommittedVisualFrame *previous = nullptr,
                                    int maxSeedCells = 4) const;

private:
    struct BuildState
    {
        VisualFrame frame{};
        std::unordered_set<TileKey, TileKeyHash> emittedUniformAuthorities{};
    };

    static void visit(const ViewportRequest &request,
                      const TileCache &tileCache,
                      const CommittedVisualFrame *previous,
                      const TileKey &key,
                      int leafLevel,
                      BuildState &state);
    static void emitFallbackCell(const ViewportRequest &request,
                                 const TileCache &tileCache,
                                 const CommittedVisualFrame *previous,
                                 const TileKey &key,
                                 BuildState &state);
    static void emitUniformAuthority(const ViewportRequest &request,
                                     const TileRecord &record,
                                     BuildState &state);
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
        bool fallback);
    [[nodiscard]] static std::optional<DisplayTile> previousVisualTileFor(
        const ViewportRequest &request,
        const TileKey &displayKey,
        const CommittedVisualFrame *previous);
    [[nodiscard]] static std::optional<DisplayTile> mixedAncestorFallbackTile(
        const ViewportRequest &request,
        const TileKey &displayKey,
        const TileRecord &record);
    [[nodiscard]] static DisplayTile missingTileFor(
        const ViewportRequest &request,
        const TileKey &key);
};
}

#endif // VISUALCOVERBUILDER_H
