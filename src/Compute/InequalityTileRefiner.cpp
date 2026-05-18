#include "InequalityTileRefiner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace gx
{
namespace
{
constexpr auto PixelFalse = uint8_t{0};
constexpr auto PixelUnknown = uint8_t{127};
constexpr auto PixelTrue = uint8_t{255};

[[nodiscard]] std::optional<int> exactLog2(const uint32_t value)
{
    if (value == 0 || (value & (value - 1)) != 0)
    {
        return std::nullopt;
    }

    auto remaining = value;
    auto log = 0;
    while (remaining > 1)
    {
        remaining >>= 1;
        ++log;
    }
    return log;
}

class RefinementRun
{
public:
    RefinementRun(const CompiledFormula &nextFormula,
                  const TileKey &nextPreviewKey,
                  InequalityTileRefinementOptions nextOptions,
                  const int nextPixelDepth)
        : formula{nextFormula},
          previewKey{nextPreviewKey},
          options{std::move(nextOptions)},
          pixelDepth{nextPixelDepth},
          pixelLevel{nextPreviewKey.level - nextPixelDepth},
          subpixelLevel{nextPreviewKey.level - nextPixelDepth
              - std::max(0, options.subpixelExtraDepth)},
          rootBounds{options.rootBounds.value_or(tileBounds(nextPreviewKey))},
          xSlot{formula.variableSlot("x")},
          ySlot{formula.variableSlot("y")},
          intervalVariables(formula.variableNames.size(), Interval{0.0})
    {
        region.key = previewKey;
        region.width = options.pixelsPerAxis;
        region.height = options.pixelsPerAxis;
        region.certainty = TextureCertainty::Precise;
        region.proofTree.rootKey = previewKey;
        region.proofTree.certainty = TextureCertainty::Precise;
        region.pixels.assign(
            static_cast<size_t>(options.pixelsPerAxis) * options.pixelsPerAxis,
            PixelUnknown);
    }

    [[nodiscard]] InequalityTileRefinementResult run()
    {
        if (cancelled())
        {
            return {.ok = false, .message = "Cancelled"};
        }

        region.existence = refineAbovePixel(previewKey, rootBounds);
        region.proofTree.existence = region.existence;
        region.proofTree.certainty = region.certainty;

        if (cancelled())
        {
            return {.ok = false, .message = "Cancelled"};
        }

        return {
            .ok = true,
            .region = std::move(region),
            .visitedNodes = visitedNodes,
            .unknownPixels = unknownPixels
        };
    }

private:
    struct EvaluatedBox
    {
        TileClassification classification{TileClassification::Mixed};
        Interval interval{0.0, 1.0};
    };

    [[nodiscard]] bool cancelled() const
    {
        return options.cancelled && options.cancelled();
    }

    [[nodiscard]] static std::array<Rect, 4> childBounds(const Rect &bounds)
    {
        const auto xMid = (bounds.xMin + bounds.xMax) * 0.5;
        const auto yMid = (bounds.yMin + bounds.yMax) * 0.5;
        return {
            Rect{bounds.xMin, xMid, bounds.yMin, yMid},
            Rect{xMid, bounds.xMax, bounds.yMin, yMid},
            Rect{bounds.xMin, xMid, yMid, bounds.yMax},
            Rect{xMid, bounds.xMax, yMid, bounds.yMax}
        };
    }

    [[nodiscard]] EvaluatedBox evaluate(const Rect &bounds)
    {
        if (xSlot)
        {
            intervalVariables[*xSlot] = Interval{bounds.xMin, bounds.xMax};
        }
        if (ySlot)
        {
            intervalVariables[*ySlot] = Interval{bounds.yMin, bounds.yMax};
        }

        try
        {
            const auto interval = formula.evaluateInterval(intervalVariables);
            if (interval.undefined() || interval.hasDiscontinuity())
            {
                return {.classification = TileClassification::Mixed, .interval = interval};
            }
            if (interval.allTrue())
            {
                return {.classification = TileClassification::UniformTrue, .interval = interval};
            }
            if (interval.allFalse())
            {
                return {.classification = TileClassification::UniformFalse, .interval = interval};
            }
            return {.classification = TileClassification::Mixed, .interval = interval};
        }
        catch (...)
        {
            return {.classification = TileClassification::Mixed, .interval = INTERVAL_UNDEFINED};
        }
    }

    void recordProofNode(const TileKey &key,
                         const EvaluatedBox &evaluated,
                         const TileExistenceState existence)
    {
        region.proofTree.nodes.push_back(TileProofNode{
            .key = key,
            .classification = evaluated.classification,
            .existence = existence,
            .interval = evaluated.interval
        });
    }

    [[nodiscard]] size_t pixelIndex(const int localX, const int localY) const
    {
        return static_cast<size_t>(localY) * options.pixelsPerAxis + static_cast<size_t>(localX);
    }

    [[nodiscard]] std::optional<std::array<int, 3>> pixelSpanFor(const TileKey &key) const
    {
        if (key.level < pixelLevel || key.level > previewKey.level)
        {
            return std::nullopt;
        }

        const auto nodeToPixelShift = key.level - pixelLevel;
        const auto previewToPixelShift = previewKey.level - pixelLevel;
        if (nodeToPixelShift >= 62 || previewToPixelShift >= 62)
        {
            return std::nullopt;
        }

        const auto span = int64_t{1} << nodeToPixelShift;
        const auto previewBaseX = previewKey.x * (int64_t{1} << previewToPixelShift);
        const auto previewBaseY = previewKey.y * (int64_t{1} << previewToPixelShift);
        const auto startX = key.x * span - previewBaseX;
        const auto startY = key.y * span - previewBaseY;
        if (startX < 0
            || startY < 0
            || startX + span > options.pixelsPerAxis
            || startY + span > options.pixelsPerAxis
            || span > std::numeric_limits<int>::max()
            || startX > std::numeric_limits<int>::max()
            || startY > std::numeric_limits<int>::max())
        {
            return std::nullopt;
        }

        return std::array{
            static_cast<int>(startX),
            static_cast<int>(startY),
            static_cast<int>(span)
        };
    }

    void fillPixelsFor(const TileKey &key, const uint8_t value)
    {
        const auto span = pixelSpanFor(key);
        if (!span)
        {
            return;
        }

        const auto [startX, startY, width] = *span;
        for (auto y = 0; y < width; ++y)
        {
            for (auto x = 0; x < width; ++x)
            {
                region.pixels[pixelIndex(startX + x, startY + y)] = value;
            }
        }
    }

    void setPixelFor(const TileKey &key, const TileExistenceState existence)
    {
        const auto span = pixelSpanFor(key);
        if (!span || (*span)[2] != 1)
        {
            return;
        }

        const auto [x, y, width] = *span;
        (void)width;
        auto value = PixelUnknown;
        if (existence == TileExistenceState::Exists)
        {
            value = PixelTrue;
        }
        else if (existence == TileExistenceState::Empty)
        {
            value = PixelFalse;
        }
        else
        {
            region.certainty = TextureCertainty::BestEstimate;
            ++unknownPixels;
        }
        region.pixels[pixelIndex(x, y)] = value;
    }

    [[nodiscard]] TileExistenceState refineSubpixel(const TileKey &key, const Rect &bounds)
    {
        if (cancelled())
        {
            return TileExistenceState::Unknown;
        }

        ++visitedNodes;
        const auto evaluated = evaluate(bounds);
        if (evaluated.classification == TileClassification::UniformTrue)
        {
            recordProofNode(key, evaluated, TileExistenceState::Exists);
            return TileExistenceState::Exists;
        }
        if (evaluated.classification == TileClassification::UniformFalse)
        {
            recordProofNode(key, evaluated, TileExistenceState::Empty);
            return TileExistenceState::Empty;
        }
        if (key.level <= subpixelLevel)
        {
            recordProofNode(key, evaluated, TileExistenceState::Unknown);
            return TileExistenceState::Unknown;
        }

        auto allEmpty = true;
        const auto children = tileChildren(key);
        const auto boundsChildren = childBounds(bounds);
        for (auto index = size_t{0}; index < children.size(); ++index)
        {
            const auto childExistence = refineSubpixel(children[index], boundsChildren[index]);
            if (childExistence == TileExistenceState::Exists)
            {
                return TileExistenceState::Exists;
            }
            if (childExistence != TileExistenceState::Empty)
            {
                allEmpty = false;
            }
        }

        const auto existence = allEmpty ? TileExistenceState::Empty : TileExistenceState::Unknown;
        recordProofNode(key, evaluated, existence);
        return existence;
    }

    [[nodiscard]] TileExistenceState refinePixel(const TileKey &key,
                                                 const Rect &bounds,
                                                 const EvaluatedBox &evaluated)
    {
        if (key.level <= subpixelLevel)
        {
            setPixelFor(key, TileExistenceState::Unknown);
            recordProofNode(key, evaluated, TileExistenceState::Unknown);
            return TileExistenceState::Unknown;
        }

        auto allEmpty = true;
        const auto children = tileChildren(key);
        const auto boundsChildren = childBounds(bounds);
        for (auto index = size_t{0}; index < children.size(); ++index)
        {
            const auto childExistence = refineSubpixel(children[index], boundsChildren[index]);
            if (childExistence == TileExistenceState::Exists)
            {
                setPixelFor(key, TileExistenceState::Exists);
                recordProofNode(key, evaluated, TileExistenceState::Exists);
                return TileExistenceState::Exists;
            }
            if (childExistence != TileExistenceState::Empty)
            {
                allEmpty = false;
            }
        }

        const auto existence = allEmpty ? TileExistenceState::Empty : TileExistenceState::Unknown;
        setPixelFor(key, existence);
        recordProofNode(key, evaluated, existence);
        return existence;
    }

    [[nodiscard]] TileExistenceState refineAbovePixel(const TileKey &key, const Rect &bounds)
    {
        if (cancelled())
        {
            return TileExistenceState::Unknown;
        }

        ++visitedNodes;
        const auto evaluated = evaluate(bounds);
        if (evaluated.classification == TileClassification::UniformTrue)
        {
            fillPixelsFor(key, PixelTrue);
            recordProofNode(key, evaluated, TileExistenceState::Exists);
            return TileExistenceState::Exists;
        }
        if (evaluated.classification == TileClassification::UniformFalse)
        {
            fillPixelsFor(key, PixelFalse);
            recordProofNode(key, evaluated, TileExistenceState::Empty);
            return TileExistenceState::Empty;
        }
        if (key.level == pixelLevel)
        {
            return refinePixel(key, bounds, evaluated);
        }
        if (key.level < pixelLevel)
        {
            return TileExistenceState::Unknown;
        }

        auto anyExists = false;
        auto allEmpty = true;
        const auto children = tileChildren(key);
        const auto boundsChildren = childBounds(bounds);
        for (auto index = size_t{0}; index < children.size(); ++index)
        {
            const auto childExistence = refineAbovePixel(children[index], boundsChildren[index]);
            anyExists = anyExists || childExistence == TileExistenceState::Exists;
            allEmpty = allEmpty && childExistence == TileExistenceState::Empty;
        }

        const auto existence = anyExists
            ? TileExistenceState::Exists
            : (allEmpty ? TileExistenceState::Empty : TileExistenceState::Unknown);
        recordProofNode(key, evaluated, existence);
        return existence;
    }

    const CompiledFormula &formula;
    TileKey previewKey{};
    InequalityTileRefinementOptions options{};
    int pixelDepth{0};
    int pixelLevel{0};
    int subpixelLevel{0};
    Rect rootBounds{};
    std::optional<size_t> xSlot{};
    std::optional<size_t> ySlot{};
    std::vector<Interval> intervalVariables;
    RegionOutput region{};
    size_t visitedNodes{0};
    size_t unknownPixels{0};
};
}

InequalityTileRefinementResult refineInequalityTile(
    const CompiledFormula &formula,
    const TileKey &previewKey,
    const InequalityTileRefinementOptions &options)
{
    const auto pixelDepth = exactLog2(options.pixelsPerAxis);
    if (!pixelDepth)
    {
        return {
            .ok = false,
            .message = "Inequality tile refinement requires a power-of-two texture size"
        };
    }

    auto run = RefinementRun{formula, previewKey, options, *pixelDepth};
    return run.run();
}
}
