#include "InequalityTileRefiner.h"
#include "AffineInequality.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <future>
#include <limits>
#include <numbers>
#include <optional>
#include <thread>
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

[[nodiscard]] int defaultParallelSplitDepth(uint32_t pixelsPerAxis)
{
    auto depth = 0;
    while (pixelsPerAxis >= 1024 && depth < 2)
    {
        ++depth;
        pixelsPerAxis /= 2;
    }
    if (depth == 0 && pixelsPerAxis >= 256)
    {
        depth = 1;
    }
    return depth;
}

[[nodiscard]] InequalityTileRefinementResult refineInequalityTileInternal(
    const CompiledFormula &formula,
    const TileKey &previewKey,
    const InequalityTileRefinementOptions &options,
    int parallelSplitDepth);

[[nodiscard]] bool containsStrictPeriodicPoint(const double lower,
                                               const double upper,
                                               const double phase,
                                               const double period)
{
    if (!std::isfinite(lower) || !std::isfinite(upper))
    {
        return true;
    }

    const auto k = std::ceil((lower - phase) / period);
    const auto point = phase + k * period;
    return point > lower && point < upper;
}

[[nodiscard]] bool containsTangentPole(const Interval &argumentRange)
{
    return containsStrictPeriodicPoint(
        argumentRange.lower,
        argumentRange.upper,
        std::numbers::pi / 2.0,
        std::numbers::pi);
}

[[nodiscard]] Interval tangentRange(const Interval &interval)
{
    constexpr auto pi = std::numbers::pi;
    if (interval.undefined())
    {
        return {1.0, 0.0, interval.hasDiscontinuity()};
    }

    if (!std::isfinite(interval.lower) || !std::isfinite(interval.upper)
        || containsStrictPeriodicPoint(interval.lower, interval.upper, pi / 2.0, pi))
    {
        return {
            -std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
            true
        };
    }

    auto lower = std::tan(interval.lower);
    auto upper = std::tan(interval.upper);
    if (lower > upper)
    {
        std::swap(lower, upper);
    }
    return {lower, upper, interval.hasDiscontinuity()};
}

[[nodiscard]] Interval sineRange(const Interval &interval)
{
    constexpr auto pi = std::numbers::pi;
    constexpr auto fullPeriod = 2.0 * pi;
    if (interval.undefined())
    {
        return {1.0, 0.0, interval.hasDiscontinuity()};
    }

    if (!std::isfinite(interval.lower) || !std::isfinite(interval.upper) || interval.size() >= fullPeriod)
    {
        return {-1.0, 1.0, interval.hasDiscontinuity()};
    }

    auto lower = std::sin(interval.lower);
    auto upper = std::sin(interval.upper);
    if (lower > upper)
    {
        std::swap(lower, upper);
    }
    if (containsStrictPeriodicPoint(interval.lower, interval.upper, pi / 2.0, fullPeriod))
    {
        upper = 1.0;
    }
    if (containsStrictPeriodicPoint(interval.lower, interval.upper, -pi / 2.0, fullPeriod))
    {
        lower = -1.0;
    }
    return {lower, upper, interval.hasDiscontinuity()};
}

[[nodiscard]] Interval expRange(const Interval &interval)
{
    if (interval.undefined())
    {
        return {1.0, 0.0, interval.hasDiscontinuity()};
    }
    return {std::exp(interval.lower), std::exp(interval.upper), interval.hasDiscontinuity()};
}

[[nodiscard]] Interval logRange(const Interval &interval)
{
    if (interval.undefined())
    {
        return {1.0, 0.0, interval.hasDiscontinuity()};
    }
    if (interval.upper <= 0.0)
    {
        return {1.0, 0.0, true};
    }
    if (interval.lower <= 0.0)
    {
        return {-std::numeric_limits<double>::infinity(), std::log(interval.upper), true};
    }
    return {std::log(interval.lower), std::log(interval.upper), interval.hasDiscontinuity()};
}

[[nodiscard]] Interval sqrtRange(const Interval &interval)
{
    if (interval.undefined())
    {
        return {1.0, 0.0, interval.hasDiscontinuity()};
    }
    if (interval.upper < 0.0)
    {
        return {1.0, 0.0, true};
    }
    if (interval.lower < 0.0)
    {
        return {0.0, std::sqrt(interval.upper), true};
    }
    return {std::sqrt(interval.lower), std::sqrt(interval.upper), interval.hasDiscontinuity()};
}

[[nodiscard]] Interval compareIntervalOperands(const Interval &lhs, const Interval &rhs, const FormulaOp comparison)
{
    const auto unresolvedDomain = lhs.hasDiscontinuity() || rhs.hasDiscontinuity();
    if (lhs.undefined() || rhs.undefined())
    {
        return {1.0, 0.0, unresolvedDomain};
    }

    switch (comparison)
    {
    case FormulaOp::Greater:
        if (lhs.lower > rhs.upper) return {1.0, 1.0, unresolvedDomain};
        if (lhs.upper <= rhs.lower) return {0.0, 0.0, unresolvedDomain};
        return {0.0, 1.0, unresolvedDomain};
    case FormulaOp::Less:
        if (lhs.upper < rhs.lower) return {1.0, 1.0, unresolvedDomain};
        if (lhs.lower >= rhs.upper) return {0.0, 0.0, unresolvedDomain};
        return {0.0, 1.0, unresolvedDomain};
    case FormulaOp::GreaterEqual:
        if (lhs.lower >= rhs.upper) return {1.0, 1.0, unresolvedDomain};
        if (lhs.upper < rhs.lower) return {0.0, 0.0, unresolvedDomain};
        return {0.0, 1.0, unresolvedDomain};
    case FormulaOp::LessEqual:
        if (lhs.upper <= rhs.lower) return {1.0, 1.0, unresolvedDomain};
        if (lhs.lower > rhs.upper) return {0.0, 0.0, unresolvedDomain};
        return {0.0, 1.0, unresolvedDomain};
    case FormulaOp::Equal:
        if (lhs.upper < rhs.lower || rhs.upper < lhs.lower)
        {
            return {0.0, 0.0, unresolvedDomain};
        }
        if (lhs.allConstant() && rhs.allConstant() && lhs.lower == rhs.lower)
        {
            return {1.0, 1.0, unresolvedDomain};
        }
        return {0.0, 1.0, unresolvedDomain};
    case FormulaOp::NotEqual:
        if (lhs.upper < rhs.lower || rhs.upper < lhs.lower)
        {
            return {1.0, 1.0, unresolvedDomain};
        }
        if (lhs.allConstant() && rhs.allConstant() && lhs.lower == rhs.lower)
        {
            return {0.0, 0.0, unresolvedDomain};
        }
        return {0.0, 1.0, unresolvedDomain};
    default:
        return INTERVAL_UNDEFINED;
    }
}

[[nodiscard]] bool compareDoubleOperands(const double lhs, const double rhs, const FormulaOp comparison)
{
    switch (comparison)
    {
    case FormulaOp::Greater:
        return lhs > rhs;
    case FormulaOp::Less:
        return lhs < rhs;
    case FormulaOp::GreaterEqual:
        return lhs >= rhs;
    case FormulaOp::LessEqual:
        return lhs <= rhs;
    case FormulaOp::Equal:
        return lhs == rhs;
    case FormulaOp::NotEqual:
        return lhs != rhs;
    default:
        return false;
    }
}

void fillTrueRange(uint8_t *row, const uint32_t count, const uint32_t trueBegin, const uint32_t trueEnd)
{
    std::fill_n(row, static_cast<size_t>(trueBegin), PixelFalse);
    std::fill_n(row + trueBegin, static_cast<size_t>(trueEnd - trueBegin), PixelTrue);
    std::fill_n(row + trueEnd, static_cast<size_t>(count - trueEnd), PixelFalse);
}

template<typename Predicate>
[[nodiscard]] uint32_t firstFalseInTruePrefix(const uint32_t count,
                                              const double first,
                                              const double step,
                                              Predicate predicate)
{
    auto low = uint32_t{0};
    auto high = count;
    while (low < high)
    {
        const auto mid = low + (high - low) / 2;
        if (predicate(first + step * static_cast<double>(mid)))
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }
    return low;
}

template<typename Predicate>
[[nodiscard]] uint32_t firstTrueInFalsePrefix(const uint32_t count,
                                              const double first,
                                              const double step,
                                              Predicate predicate)
{
    auto low = uint32_t{0};
    auto high = count;
    while (low < high)
    {
        const auto mid = low + (high - low) / 2;
        if (predicate(first + step * static_cast<double>(mid)))
        {
            high = mid;
        }
        else
        {
            low = mid + 1;
        }
    }
    return low;
}

template<typename Predicate>
void fillLessLikeMonotoneRun(uint8_t *row,
                             const uint32_t count,
                             const double first,
                             const double step,
                             Predicate predicate)
{
    if (step > 0.0)
    {
        fillTrueRange(row, count, 0, firstFalseInTruePrefix(count, first, step, predicate));
    }
    else if (step < 0.0)
    {
        fillTrueRange(row, count, firstTrueInFalsePrefix(count, first, step, predicate), count);
    }
    else
    {
        std::fill_n(row, static_cast<size_t>(count), predicate(first) ? PixelTrue : PixelFalse);
    }
}

template<typename Predicate>
void fillGreaterLikeMonotoneRun(uint8_t *row,
                                const uint32_t count,
                                const double first,
                                const double step,
                                Predicate predicate)
{
    if (step > 0.0)
    {
        fillTrueRange(row, count, firstTrueInFalsePrefix(count, first, step, predicate), count);
    }
    else if (step < 0.0)
    {
        fillTrueRange(row, count, 0, firstFalseInTruePrefix(count, first, step, predicate));
    }
    else
    {
        std::fill_n(row, static_cast<size_t>(count), predicate(first) ? PixelTrue : PixelFalse);
    }
}

class RefinementRun
{
    struct EvaluatedBox
    {
        TileClassification classification{TileClassification::Mixed};
        Interval interval{0.0, 1.0};
    };

    struct IntervalLevelCache
    {
        int level{0};
        int64_t baseIndex{0};
        std::vector<Interval> values{};
        std::vector<uint8_t> valid{};
    };

    struct PointLevelCache
    {
        int level{0};
        int64_t baseIndex{0};
        std::vector<double> centers{};
        std::vector<uint8_t> centerValid{};
        std::vector<double> boundaries{};
        std::vector<uint8_t> boundaryValid{};
    };

    enum class DirectOperandKind
    {
        Bytecode,
        Constant,
        XVariable,
        YVariable,
        UnaryVariable,
        SinProductXY,
        SquareDifferenceXY,
        SumSquaresXY,
        UnaryXMinusY,
        YMinusUnaryX,
        ReciprocalShiftX
    };

    struct OperandPlan
    {
        FormulaBytecodeSlice slice{};
        DirectOperandKind kind{DirectOperandKind::Bytecode};
        double constant{0.0};
        double secondaryConstant{0.0};
        size_t variableSlot{0};
        std::array<FormulaOp, 3> unaryOps{};
        size_t unaryCount{0};
    };

    struct RootAxisOnlyOperandCache
    {
        static constexpr auto MaxCachedAxisShift = 16;
        bool axisIsY{true};
        bool cachedOperandIsLhs{false};
        OperandPlan cachedOperand{};
        OperandPlan otherOperand{};
        std::array<std::optional<IntervalLevelCache>, MaxCachedAxisShift + 1> intervalLevels{};
        std::array<std::optional<PointLevelCache>, MaxCachedAxisShift + 1> pointLevels{};
        std::array<int64_t, MaxCachedAxisShift + 1> scaleByShift{};
        std::array<int64_t, MaxCachedAxisShift + 1> baseIndexByShift{};
        std::array<uint8_t, MaxCachedAxisShift + 1> addressValid{};
    };

    struct RootComparisonDirectPlan
    {
        FormulaOp comparison{FormulaOp::LessEqual};
        OperandPlan lhs{};
        OperandPlan rhs{};
    };

    struct MonotoneUnaryCurvePlan
    {
        FormulaOp unaryOp{FormulaOp::Log};
        FormulaOp yComparison{FormulaOp::Less};
    };

    struct ReciprocalCurvePlan
    {
        double numerator{1.0};
        double shift{0.0};
        FormulaOp yComparison{FormulaOp::Less};
    };

    struct TangentAxisCurvePlan
    {
        bool tangentAxisIsX{true};
        double tangentScale{1.0};
        FormulaOp tangentComparison{FormulaOp::Greater};
    };

    struct SinProductAxisComparisonPlan
    {
        double sineScale{1.0};
        FormulaOp sineComparison{FormulaOp::Less};
    };

    struct SumSquaresDiskPlan
    {
        double radiusSquared{0.0};
        FormulaOp comparison{FormulaOp::Less};
    };

    struct SquareDifferenceBandPlan
    {
        double widthSquared{0.0};
        FormulaOp comparison{FormulaOp::Less};
    };

    enum class PointAxisKind
    {
        Center,
        LowerBoundary,
        UpperBoundary
    };

    struct AxisCacheAddress
    {
        size_t shiftIndex{0};
        int64_t scale{1};
        int64_t baseIndex{0};
        size_t relativeIndex{0};
    };

public:
    RefinementRun(const CompiledFormula &nextFormula,
                  const TileKey &nextPreviewKey,
                  InequalityTileRefinementOptions nextOptions,
                  const int nextPixelDepth,
                  const int nextParallelSplitDepth)
        : formula{nextFormula},
          previewKey{nextPreviewKey},
          options{std::move(nextOptions)},
          pixelDepth{nextPixelDepth},
          parallelSplitDepth{nextParallelSplitDepth},
          pixelLevel{nextPreviewKey.level - nextPixelDepth},
          subpixelLevel{nextPreviewKey.level - nextPixelDepth
              - std::max(0, options.subpixelExtraDepth)},
          rootBounds{options.rootBounds.value_or(tileBounds(nextPreviewKey))},
          hasCancellationCallback{static_cast<bool>(options.cancelled)},
          hasNodeBudget{options.maxVisitedNodes > 0},
          maxVisitedNodes{options.maxVisitedNodes},
          recordOnlyRootProof{!options.recordDetailedProofTree},
          xSlot{formula.variableSlot("x")},
          ySlot{formula.variableSlot("y")},
          intervalVariables(formula.variableNames.size(), Interval{0.0}),
          doubleVariables(formula.variableNames.size(), 0.0)
    {
        evaluationStack.reserve(formula.evaluationIr.size());
        doubleEvaluationStack.reserve(formula.evaluationIr.size());
        region.key = previewKey;
        region.width = options.pixelsPerAxis;
        region.height = options.pixelsPerAxis;
        region.certainty = TextureCertainty::Precise;
        region.proofTree.rootKey = previewKey;
        region.proofTree.certainty = TextureCertainty::Precise;
        region.pixels.assign(
            static_cast<size_t>(options.pixelsPerAxis) * options.pixelsPerAxis,
            PixelUnknown);
        initializeRootAxisOnlyOperandCache();
        initializeRootComparisonDirectPlan();
        initializeSinProductAxisComparisonPlan();
        initializeSumSquaresDiskPlan();
        initializeSquareDifferenceBandPlan();
        initializeReciprocalCurvePlan();
        initializeTangentAxisCurvePlan();
        initializeMonotoneUnaryCurvePlan();
    }

    [[nodiscard]] InequalityTileRefinementResult run()
    {
        if (cancelled())
        {
            return {.ok = false, .message = "Cancelled"};
        }

        if (formula.tangentPoleInequality)
        {
            if (auto tangentPoleResult = runTangentPoleIfEveryPixel(*formula.tangentPoleInequality))
            {
                return std::move(*tangentPoleResult);
            }
        }

        if (formula.affineInequality)
        {
            return runAffine(*formula.affineInequality);
        }
        if (auto diskResult = runSumSquaresDiskIfEveryPixel())
        {
            return std::move(*diskResult);
        }
        if (auto bandResult = runSquareDifferenceBandIfEveryPixel())
        {
            return std::move(*bandResult);
        }
        if (auto reciprocalCurveResult = runReciprocalCurveIfEveryPixel())
        {
            return std::move(*reciprocalCurveResult);
        }
        if (auto tangentAxisResult = runTangentAxisCurveIfEveryPixel())
        {
            return std::move(*tangentAxisResult);
        }
        if (auto monotoneCurveResult = runMonotoneUnaryCurveIfEveryPixel())
        {
            return std::move(*monotoneCurveResult);
        }
        if (auto parallelResult = runParallelChildRegionsIfUseful())
        {
            return std::move(*parallelResult);
        }

        region.existence = refineAbovePixel(previewKey, rootBounds);
        region.proofTree.existence = region.existence;
        region.proofTree.certainty = region.certainty;

        if (nodeBudgetExhausted())
        {
            region.certainty = TextureCertainty::Imprecise;
            region.proofTree.certainty = TextureCertainty::Imprecise;
        }

        if (cancelled())
        {
            return {.ok = false, .message = "Cancelled"};
        }

        return {
            .ok = true,
            .region = std::move(region),
            .visitedNodes = visitedNodes,
            .unknownPixels = unknownPixels,
            .intervalEvaluations = intervalEvaluations,
            .pointEvaluations = pointEvaluations
        };
    }

private:
    [[nodiscard]] bool cancelled() const
    {
        return hasCancellationCallback && options.cancelled();
    }

    [[nodiscard]] bool nodeBudgetExhausted() const
    {
        return hasNodeBudget && visitedNodes >= maxVisitedNodes;
    }

    [[nodiscard]] std::optional<InequalityTileRefinementResult> runParallelChildRegionsIfUseful()
    {
        constexpr auto MinParallelPixels = uint32_t{256};
        if (options.recordDetailedProofTree
            || parallelSplitDepth <= 0
            || options.pixelsPerAxis < MinParallelPixels
            || options.pixelsPerAxis % 2 != 0
            || std::thread::hardware_concurrency() < 4
            || previewKey.level <= pixelLevel)
        {
            return std::nullopt;
        }

        ++visitedNodes;
        const auto evaluated = evaluate(previewKey, rootBounds);
        if (evaluated.classification == TileClassification::UniformTrue)
        {
            fillPixelsFor(previewKey, PixelTrue);
            recordProofNode(previewKey, evaluated, TileExistenceState::Exists);
            region.existence = TileExistenceState::Exists;
            region.proofTree.existence = region.existence;
            region.proofTree.certainty = region.certainty;
            return InequalityTileRefinementResult{
                .ok = true,
                .region = std::move(region),
                .visitedNodes = visitedNodes,
                .unknownPixels = unknownPixels,
                .intervalEvaluations = intervalEvaluations,
                .pointEvaluations = pointEvaluations
            };
        }
        if (evaluated.classification == TileClassification::UniformFalse)
        {
            fillPixelsFor(previewKey, PixelFalse);
            recordProofNode(previewKey, evaluated, TileExistenceState::Empty);
            region.existence = TileExistenceState::Empty;
            region.proofTree.existence = region.existence;
            region.proofTree.certainty = region.certainty;
            return InequalityTileRefinementResult{
                .ok = true,
                .region = std::move(region),
                .visitedNodes = visitedNodes,
                .unknownPixels = unknownPixels,
                .intervalEvaluations = intervalEvaluations,
                .pointEvaluations = pointEvaluations
            };
        }
        if (evaluated.interval.undefined())
        {
            fillPixelsFor(previewKey, PixelUnknown);
            recordProofNode(previewKey, evaluated, TileExistenceState::Unknown);
            region.existence = TileExistenceState::Unknown;
            region.proofTree.existence = region.existence;
            region.proofTree.certainty = region.certainty;
            return InequalityTileRefinementResult{
                .ok = true,
                .region = std::move(region),
                .visitedNodes = visitedNodes,
                .unknownPixels = unknownPixels,
                .intervalEvaluations = intervalEvaluations,
                .pointEvaluations = pointEvaluations
            };
        }

        const auto childPixels = options.pixelsPerAxis / 2;
        const auto children = tileChildren(previewKey);
        const auto boundsChildren = childBounds(rootBounds);
        auto childOptions = options;
        childOptions.pixelsPerAxis = childPixels;
        childOptions.recordDetailedProofTree = false;

        std::array<std::future<InequalityTileRefinementResult>, 4> futures;
        for (auto index = size_t{0}; index < children.size(); ++index)
        {
            futures[index] = std::async(
                std::launch::async,
                [this, childOptions, child = children[index], bounds = boundsChildren[index]]() mutable
                {
                    childOptions.rootBounds = bounds;
                    return refineInequalityTileInternal(formula, child, childOptions, parallelSplitDepth - 1);
                });
        }

        auto childResults = std::array<InequalityTileRefinementResult, 4>{};
        for (auto index = size_t{0}; index < futures.size(); ++index)
        {
            childResults[index] = futures[index].get();
            if (!childResults[index].ok)
            {
                return InequalityTileRefinementResult{
                    .ok = false,
                    .message = childResults[index].message,
                    .visitedNodes = visitedNodes,
                    .unknownPixels = unknownPixels,
                    .intervalEvaluations = intervalEvaluations,
                    .pointEvaluations = pointEvaluations
                };
            }
        }

        auto anyExists = false;
        auto allEmpty = true;
        auto combinedCertainty = region.certainty;
        const auto mergeCertainty = [](const TextureCertainty lhs, const TextureCertainty rhs)
        {
            if (lhs == TextureCertainty::BestEstimate || rhs == TextureCertainty::BestEstimate)
            {
                return TextureCertainty::BestEstimate;
            }
            if (lhs == TextureCertainty::Imprecise || rhs == TextureCertainty::Imprecise)
            {
                return TextureCertainty::Imprecise;
            }
            return TextureCertainty::Precise;
        };
        for (auto index = size_t{0}; index < childResults.size(); ++index)
        {
            auto &child = childResults[index];
            const auto offsetX = (index % 2) == 0 ? uint32_t{0} : childPixels;
            const auto offsetY = index < 2 ? uint32_t{0} : childPixels;
            for (uint32_t y = 0; y < childPixels; ++y)
            {
                const auto source = child.region.pixels.begin()
                    + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * childPixels);
                const auto target = region.pixels.begin()
                    + static_cast<std::ptrdiff_t>(
                        static_cast<size_t>(offsetY + y) * options.pixelsPerAxis + offsetX);
                std::copy_n(source, static_cast<size_t>(childPixels), target);
            }

            visitedNodes += child.visitedNodes;
            unknownPixels += child.unknownPixels;
            intervalEvaluations += child.intervalEvaluations;
            pointEvaluations += child.pointEvaluations;
            anyExists = anyExists || child.region.existence == TileExistenceState::Exists;
            allEmpty = allEmpty && child.region.existence == TileExistenceState::Empty;
            combinedCertainty = mergeCertainty(combinedCertainty, child.region.certainty);
        }

        region.existence = anyExists
            ? TileExistenceState::Exists
            : (allEmpty ? TileExistenceState::Empty : TileExistenceState::Unknown);
        region.certainty = nodeBudgetExhausted() ? TextureCertainty::Imprecise : combinedCertainty;
        recordProofNode(previewKey, evaluated, region.existence);
        region.proofTree.existence = region.existence;
        region.proofTree.certainty = region.certainty;
        return InequalityTileRefinementResult{
            .ok = true,
            .region = std::move(region),
            .visitedNodes = visitedNodes,
            .unknownPixels = unknownPixels,
            .intervalEvaluations = intervalEvaluations,
            .pointEvaluations = pointEvaluations
        };
    }

    [[nodiscard]] InequalityTileRefinementResult runAffine(const AffineInequality &affine)
    {
        ++visitedNodes;
        const auto rootValueRange = affineValueRange(affine, rootBounds);
        const auto rootTruthRange = truthRangeForAffineValue(rootValueRange, affine.comparison);
        const auto rootClassification = classificationForTruthRange(rootTruthRange);
        region.existence = affineCanBeTrue(rootValueRange, affine.comparison)
            ? TileExistenceState::Exists
            : TileExistenceState::Empty;

        if (rootClassification == TileClassification::UniformTrue)
        {
            fillPixelsFor(previewKey, PixelTrue);
        }
        else if (rootClassification == TileClassification::UniformFalse)
        {
            fillPixelsFor(previewKey, PixelFalse);
        }
        else
        {
            const auto xStep = (rootBounds.xMax - rootBounds.xMin)
                / static_cast<double>(options.pixelsPerAxis);
            const auto yStep = (rootBounds.yMax - rootBounds.yMin)
                / static_cast<double>(options.pixelsPerAxis);
            if (affine.yCoefficient == 0.0)
            {
                std::vector<uint8_t> row(options.pixelsPerAxis, PixelFalse);
                for (uint32_t x = 0; x < options.pixelsPerAxis; ++x)
                {
                    const auto xMin = rootBounds.xMin + static_cast<double>(x) * xStep;
                    const auto xMax = x == options.pixelsPerAxis - 1
                        ? rootBounds.xMax
                        : xMin + xStep;
                    const auto pixelValueRange = affineValueRange(
                        affine,
                        Rect{xMin, xMax, rootBounds.yMin, rootBounds.yMax});
                    row[x] =
                        affineCanBeTrue(pixelValueRange, affine.comparison) ? PixelTrue : PixelFalse;
                }
                for (uint32_t y = 0; y < options.pixelsPerAxis; ++y)
                {
                    std::copy(
                        row.begin(),
                        row.end(),
                        region.pixels.begin() + static_cast<std::ptrdiff_t>(
                            static_cast<size_t>(y) * options.pixelsPerAxis));
                }
            }
            else if (affine.xCoefficient == 0.0)
            {
                for (uint32_t y = 0; y < options.pixelsPerAxis; ++y)
                {
                    const auto yMin = rootBounds.yMin + static_cast<double>(y) * yStep;
                    const auto yMax = y == options.pixelsPerAxis - 1
                        ? rootBounds.yMax
                        : yMin + yStep;
                    const auto pixelValueRange = affineValueRange(
                        affine,
                        Rect{rootBounds.xMin, rootBounds.xMax, yMin, yMax});
                    const auto value =
                        affineCanBeTrue(pixelValueRange, affine.comparison) ? PixelTrue : PixelFalse;
                    std::fill_n(
                        region.pixels.begin() + static_cast<std::ptrdiff_t>(
                            static_cast<size_t>(y) * options.pixelsPerAxis),
                        static_cast<size_t>(options.pixelsPerAxis),
                        value);
                }
            }
            else
            {
                const auto pixelCount = options.pixelsPerAxis;
                const auto lastX = pixelCount - 1;
                for (uint32_t y = 0; y < options.pixelsPerAxis; ++y)
                {
                    const auto yMin = rootBounds.yMin + static_cast<double>(y) * yStep;
                    const auto yMax = y == options.pixelsPerAxis - 1
                        ? rootBounds.yMax
                        : yMin + yStep;
                    const auto rowLower = affine.constant
                        + (affine.yCoefficient >= 0.0
                            ? affine.yCoefficient * yMin
                            : affine.yCoefficient * yMax);
                    const auto rowUpper = affine.constant
                        + (affine.yCoefficient >= 0.0
                            ? affine.yCoefficient * yMax
                            : affine.yCoefficient * yMin);
                    const auto initialXMax = options.pixelsPerAxis == 1
                        ? rootBounds.xMax
                        : rootBounds.xMin + xStep;
                    auto lower = rowLower
                        + (affine.xCoefficient >= 0.0
                            ? affine.xCoefficient * rootBounds.xMin
                            : affine.xCoefficient * initialXMax);
                    auto upper = rowUpper
                        + (affine.xCoefficient >= 0.0
                            ? affine.xCoefficient * initialXMax
                            : affine.xCoefficient * rootBounds.xMin);
                    const auto xRangeStep = affine.xCoefficient * xStep;
                    auto *row = region.pixels.data() + static_cast<size_t>(y) * pixelCount;
                    auto usedMonotoneFill = true;
                    switch (affine.comparison)
                    {
                    case FormulaOp::Less:
                        fillLessLikeMonotoneRun(
                            row,
                            pixelCount,
                            lower,
                            xRangeStep,
                            [](const double value) { return value < 0.0; });
                        break;
                    case FormulaOp::LessEqual:
                        fillLessLikeMonotoneRun(
                            row,
                            pixelCount,
                            lower,
                            xRangeStep,
                            [](const double value) { return value <= 0.0; });
                        break;
                    case FormulaOp::Greater:
                        fillGreaterLikeMonotoneRun(
                            row,
                            pixelCount,
                            upper,
                            xRangeStep,
                            [](const double value) { return value > 0.0; });
                        break;
                    case FormulaOp::GreaterEqual:
                        fillGreaterLikeMonotoneRun(
                            row,
                            pixelCount,
                            upper,
                            xRangeStep,
                            [](const double value) { return value >= 0.0; });
                        break;
                    default:
                        usedMonotoneFill = false;
                        for (uint32_t x = 0; x < pixelCount; ++x)
                        {
                            if (x == lastX)
                            {
                                const auto xMin = rootBounds.xMin + static_cast<double>(x) * xStep;
                                lower = rowLower
                                    + (affine.xCoefficient >= 0.0
                                        ? affine.xCoefficient * xMin
                                        : affine.xCoefficient * rootBounds.xMax);
                                upper = rowUpper
                                    + (affine.xCoefficient >= 0.0
                                        ? affine.xCoefficient * rootBounds.xMax
                                        : affine.xCoefficient * xMin);
                            }
                            row[x] = affineCanBeTrue(lower, upper, affine.comparison)
                                ? PixelTrue
                                : PixelFalse;
                            lower += xRangeStep;
                            upper += xRangeStep;
                        }
                        break;
                    }

                    if (usedMonotoneFill)
                    {
                        const auto xMin = rootBounds.xMin + static_cast<double>(lastX) * xStep;
                        const auto lastLower = rowLower
                            + (affine.xCoefficient >= 0.0
                                ? affine.xCoefficient * xMin
                                : affine.xCoefficient * rootBounds.xMax);
                        const auto lastUpper = rowUpper
                            + (affine.xCoefficient >= 0.0
                                ? affine.xCoefficient * rootBounds.xMax
                                : affine.xCoefficient * xMin);
                        row[lastX] = affineCanBeTrue(lastLower, lastUpper, affine.comparison)
                            ? PixelTrue
                            : PixelFalse;
                    }
                }
            }
        }

        recordProofNode(
            previewKey,
            EvaluatedBox{
                .classification = rootClassification,
                .interval = rootTruthRange
            },
            region.existence);
        region.proofTree.existence = region.existence;
        region.proofTree.certainty = region.certainty;
        return {
            .ok = true,
            .region = std::move(region),
            .visitedNodes = visitedNodes,
            .unknownPixels = unknownPixels,
            .intervalEvaluations = intervalEvaluations,
            .pointEvaluations = pointEvaluations
        };
    }

    [[nodiscard]] static bool supportedMonotoneUnaryCurveOp(const FormulaOp op)
    {
        return op == FormulaOp::Log
            || op == FormulaOp::Exp
            || op == FormulaOp::Sqrt;
    }

    [[nodiscard]] static bool supportedComparisonForCurve(const FormulaOp op)
    {
        return op == FormulaOp::Less
            || op == FormulaOp::LessEqual
            || op == FormulaOp::Greater
            || op == FormulaOp::GreaterEqual;
    }

    [[nodiscard]] static FormulaOp invertedComparison(const FormulaOp op)
    {
        switch (op)
        {
        case FormulaOp::Less:
            return FormulaOp::Greater;
        case FormulaOp::LessEqual:
            return FormulaOp::GreaterEqual;
        case FormulaOp::Greater:
            return FormulaOp::Less;
        case FormulaOp::GreaterEqual:
            return FormulaOp::LessEqual;
        default:
            return op;
        }
    }

    [[nodiscard]] static std::optional<FormulaOp> yComparisonForFxMinusY(const FormulaOp comparison)
    {
        switch (comparison)
        {
        case FormulaOp::Greater:
            return FormulaOp::Less;
        case FormulaOp::GreaterEqual:
            return FormulaOp::LessEqual;
        case FormulaOp::Less:
            return FormulaOp::Greater;
        case FormulaOp::LessEqual:
            return FormulaOp::GreaterEqual;
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] double xMinForColumn(const uint32_t x, const double xStep) const
    {
        return rootBounds.xMin + static_cast<double>(x) * xStep;
    }

    [[nodiscard]] double xMaxForColumn(const uint32_t x, const double xStep) const
    {
        return x == options.pixelsPerAxis - 1
            ? rootBounds.xMax
            : rootBounds.xMin + static_cast<double>(x + 1) * xStep;
    }

    template<typename Predicate>
    [[nodiscard]] uint32_t firstColumnMatching(uint32_t low, uint32_t high, Predicate predicate) const
    {
        while (low < high)
        {
            const auto mid = low + (high - low) / 2;
            if (predicate(mid))
            {
                high = mid;
            }
            else
            {
                low = mid + 1;
            }
        }
        return low;
    }

    [[nodiscard]] uint32_t monotoneCurveDomainEnd(const double xStep) const
    {
        if (monotoneUnaryCurvePlan->unaryOp == FormulaOp::Log)
        {
            return firstColumnMatching(0, options.pixelsPerAxis, [&](const uint32_t x)
            {
                return xMinForColumn(x, xStep) > 0.0;
            });
        }
        if (monotoneUnaryCurvePlan->unaryOp == FormulaOp::Sqrt)
        {
            return firstColumnMatching(0, options.pixelsPerAxis, [&](const uint32_t x)
            {
                return xMinForColumn(x, xStep) >= 0.0;
            });
        }
        return 0;
    }

    [[nodiscard]] std::optional<double> inverseThresholdForCurve(const double y) const
    {
        switch (monotoneUnaryCurvePlan->unaryOp)
        {
        case FormulaOp::Log:
            return std::exp(y);
        case FormulaOp::Exp:
            if (y <= 0.0)
            {
                return std::nullopt;
            }
            return std::log(y);
        case FormulaOp::Sqrt:
            if (y < 0.0)
            {
                return std::nullopt;
            }
            return y * y;
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] InequalityTileRefinementResult runMonotoneCurveRows()
    {
        ++visitedNodes;
        const auto pixelCount = options.pixelsPerAxis;
        const auto xStep = (rootBounds.xMax - rootBounds.xMin) / static_cast<double>(pixelCount);
        const auto yStep = (rootBounds.yMax - rootBounds.yMin) / static_cast<double>(pixelCount);
        const auto domainEnd = monotoneCurveDomainEnd(xStep);
        auto unknownPrefixEnd = domainEnd;
        if (monotoneUnaryCurvePlan->unaryOp == FormulaOp::Sqrt && domainEnd > 0)
        {
            unknownPrefixEnd = firstColumnMatching(0, domainEnd, [&](const uint32_t x)
            {
                return xMaxForColumn(x, xStep) >= 0.0;
            });
        }
        if (unknownPrefixEnd > 0)
        {
            region.certainty = TextureCertainty::BestEstimate;
            region.proofTree.certainty = TextureCertainty::BestEstimate;
            unknownPixels += static_cast<size_t>(unknownPrefixEnd) * pixelCount;
        }

        auto anyExists = false;
        auto allEmpty = unknownPrefixEnd == 0;
        for (uint32_t y = 0; y < pixelCount; ++y)
        {
            if (cancelled())
            {
                return {
                    .ok = false,
                    .message = "Cancelled",
                    .visitedNodes = visitedNodes,
                    .unknownPixels = unknownPixels,
                    .intervalEvaluations = intervalEvaluations,
                    .pointEvaluations = pointEvaluations
                };
            }

            auto *row = region.pixels.data() + static_cast<size_t>(y) * pixelCount;
            std::fill_n(row, static_cast<size_t>(unknownPrefixEnd), PixelUnknown);

            switch (monotoneUnaryCurvePlan->yComparison)
            {
            case FormulaOp::Less:
            case FormulaOp::LessEqual:
            {
                const auto yMin = rootBounds.yMin + static_cast<double>(y) * yStep;
                const auto maybeThreshold = inverseThresholdForCurve(yMin);
                const auto firstTrue = !maybeThreshold
                    ? domainEnd
                    : (monotoneUnaryCurvePlan->yComparison == FormulaOp::Less
                        ? firstColumnMatching(domainEnd, pixelCount, [&](const uint32_t x)
                        {
                            return xMaxForColumn(x, xStep) > *maybeThreshold;
                        })
                        : firstColumnMatching(domainEnd, pixelCount, [&](const uint32_t x)
                        {
                            return xMaxForColumn(x, xStep) >= *maybeThreshold;
                        }));
                std::fill_n(row + domainEnd, static_cast<size_t>(firstTrue - domainEnd), PixelFalse);
                std::fill_n(row + firstTrue, static_cast<size_t>(pixelCount - firstTrue), PixelTrue);
                anyExists = anyExists || firstTrue < pixelCount;
                allEmpty = allEmpty && firstTrue == pixelCount;
                break;
            }
            case FormulaOp::Greater:
            case FormulaOp::GreaterEqual:
            {
                const auto yMax = y == pixelCount - 1
                    ? rootBounds.yMax
                    : rootBounds.yMin + static_cast<double>(y + 1) * yStep;
                const auto maybeThreshold = inverseThresholdForCurve(yMax);
                const auto firstFalse = !maybeThreshold
                    ? domainEnd
                    : (monotoneUnaryCurvePlan->yComparison == FormulaOp::Greater
                        ? firstColumnMatching(domainEnd, pixelCount, [&](const uint32_t x)
                        {
                            return xMinForColumn(x, xStep) >= *maybeThreshold;
                        })
                        : firstColumnMatching(domainEnd, pixelCount, [&](const uint32_t x)
                        {
                            return xMinForColumn(x, xStep) > *maybeThreshold;
                        }));
                std::fill_n(row + domainEnd, static_cast<size_t>(firstFalse - domainEnd), PixelTrue);
                std::fill_n(row + firstFalse, static_cast<size_t>(pixelCount - firstFalse), PixelFalse);
                anyExists = anyExists || firstFalse > domainEnd;
                allEmpty = allEmpty && firstFalse == domainEnd;
                break;
            }
            default:
                break;
            }
        }

        if (monotoneUnaryCurvePlan->unaryOp == FormulaOp::Sqrt)
        {
            for (auto x = unknownPrefixEnd; x < domainEnd; ++x)
            {
                if (cancelled())
                {
                    return {
                        .ok = false,
                        .message = "Cancelled",
                        .visitedNodes = visitedNodes,
                        .unknownPixels = unknownPixels,
                        .intervalEvaluations = intervalEvaluations,
                        .pointEvaluations = pointEvaluations
                    };
                }

                const auto curveMax = std::sqrt(std::max(0.0, xMaxForColumn(x, xStep)));
                for (uint32_t y = 0; y < pixelCount; ++y)
                {
                    const auto yMin = rootBounds.yMin + static_cast<double>(y) * yStep;
                    const auto yMax = y == pixelCount - 1
                        ? rootBounds.yMax
                        : rootBounds.yMin + static_cast<double>(y + 1) * yStep;
                    auto exists = false;
                    switch (monotoneUnaryCurvePlan->yComparison)
                    {
                    case FormulaOp::Less:
                        exists = yMin < curveMax;
                        break;
                    case FormulaOp::LessEqual:
                        exists = yMin <= curveMax;
                        break;
                    case FormulaOp::Greater:
                        exists = yMax > 0.0;
                        break;
                    case FormulaOp::GreaterEqual:
                        exists = yMax >= 0.0;
                        break;
                    default:
                        break;
                    }
                    region.pixels[static_cast<size_t>(y) * pixelCount + x] =
                        exists ? PixelTrue : PixelFalse;
                    anyExists = anyExists || exists;
                    allEmpty = allEmpty && !exists;
                }
            }
        }

        region.existence = anyExists
            ? TileExistenceState::Exists
            : (allEmpty ? TileExistenceState::Empty : TileExistenceState::Unknown);
        region.proofTree.existence = region.existence;
        region.proofTree.certainty = region.certainty;
        recordProofNode(
            previewKey,
            EvaluatedBox{
                .classification = TileClassification::Mixed,
                .interval = Interval{0.0, 1.0, domainEnd > 0}
            },
            region.existence);
        return {
            .ok = true,
            .region = std::move(region),
            .visitedNodes = visitedNodes,
            .unknownPixels = unknownPixels,
            .intervalEvaluations = intervalEvaluations,
            .pointEvaluations = pointEvaluations
        };
    }

    [[nodiscard]] std::optional<InequalityTileRefinementResult> runMonotoneUnaryCurveIfEveryPixel()
    {
        if (!monotoneUnaryCurvePlan || options.pixelsPerAxis < 64)
        {
            return std::nullopt;
        }
        return runMonotoneCurveRows();
    }

    [[nodiscard]] Interval reciprocalCurveRangeForColumn(const double xMin, const double xMax) const
    {
        const auto numerator = reciprocalCurvePlan->numerator;
        const auto shift = reciprocalCurvePlan->shift;
        if (numerator == 0.0)
        {
            return {0.0, 0.0};
        }

        auto lower = std::numeric_limits<double>::infinity();
        auto upper = -std::numeric_limits<double>::infinity();
        const auto extend = [&](const double branchLower, const double branchUpper)
        {
            lower = std::min(lower, branchLower);
            upper = std::max(upper, branchUpper);
        };
        const auto extendFiniteBranch = [&](const double left, const double right)
        {
            const auto leftValue = numerator / (left - shift);
            const auto rightValue = numerator / (right - shift);
            extend(std::min(leftValue, rightValue), std::max(leftValue, rightValue));
        };

        if (xMin < shift)
        {
            if (xMax >= shift)
            {
                if (numerator > 0.0)
                {
                    extend(-std::numeric_limits<double>::infinity(), numerator / (xMin - shift));
                }
                else
                {
                    extend(numerator / (xMin - shift), std::numeric_limits<double>::infinity());
                }
            }
            else
            {
                extendFiniteBranch(xMin, xMax);
            }
        }

        if (xMax > shift)
        {
            if (xMin <= shift)
            {
                if (numerator > 0.0)
                {
                    extend(numerator / (xMax - shift), std::numeric_limits<double>::infinity());
                }
                else
                {
                    extend(-std::numeric_limits<double>::infinity(), numerator / (xMax - shift));
                }
            }
            else
            {
                extendFiniteBranch(xMin, xMax);
            }
        }

        if (lower > upper)
        {
            return {1.0, 0.0, true};
        }
        return {lower, upper};
    }

    [[nodiscard]] std::optional<InequalityTileRefinementResult> runReciprocalCurveIfEveryPixel()
    {
        if (!reciprocalCurvePlan || options.pixelsPerAxis < 64)
        {
            return std::nullopt;
        }
        if (reciprocalCurvePlan->numerator <= 0.0
            || (reciprocalCurvePlan->yComparison != FormulaOp::Less
                && reciprocalCurvePlan->yComparison != FormulaOp::LessEqual))
        {
            return std::nullopt;
        }

        ++visitedNodes;
        const auto pixelCount = options.pixelsPerAxis;
        const auto xStep = (rootBounds.xMax - rootBounds.xMin) / static_cast<double>(pixelCount);
        const auto yStep = (rootBounds.yMax - rootBounds.yMin) / static_cast<double>(pixelCount);
        std::vector<double> columnUpper(pixelCount, 0.0);
        for (uint32_t x = 0; x < pixelCount; ++x)
        {
            const auto valueRange = reciprocalCurveRangeForColumn(xMinForColumn(x, xStep), xMaxForColumn(x, xStep));
            columnUpper[x] = valueRange.upper;
        }
        const auto rightStart = firstColumnMatching(0, pixelCount, [&](const uint32_t x)
        {
            return xMaxForColumn(x, xStep) > reciprocalCurvePlan->shift;
        });
        const auto pastTrue = [&](const uint32_t x, const double yMin)
        {
            return reciprocalCurvePlan->yComparison == FormulaOp::Less
                ? columnUpper[x] <= yMin
                : columnUpper[x] < yMin;
        };

        auto anyExists = false;
        auto allEmpty = true;

        for (uint32_t y = 0; y < pixelCount; ++y)
        {
            if (cancelled())
            {
                return InequalityTileRefinementResult{
                    .ok = false,
                    .message = "Cancelled",
                    .visitedNodes = visitedNodes,
                    .unknownPixels = unknownPixels,
                    .intervalEvaluations = intervalEvaluations,
                    .pointEvaluations = pointEvaluations
                };
            }

            const auto yMin = rootBounds.yMin + static_cast<double>(y) * yStep;
            auto *row = region.pixels.data() + static_cast<size_t>(y) * pixelCount;
            std::fill_n(row, static_cast<size_t>(pixelCount), PixelFalse);

            const auto leftEnd = firstColumnMatching(0, rightStart, [&](const uint32_t x)
            {
                return pastTrue(x, yMin);
            });
            const auto rightEnd = firstColumnMatching(rightStart, pixelCount, [&](const uint32_t x)
            {
                return pastTrue(x, yMin);
            });

            if (leftEnd > 0)
            {
                std::fill_n(row, static_cast<size_t>(leftEnd), PixelTrue);
            }
            if (rightEnd > rightStart)
            {
                std::fill_n(row + rightStart, static_cast<size_t>(rightEnd - rightStart), PixelTrue);
            }
            const auto rowExists = leftEnd > 0 || rightEnd > rightStart;
            anyExists = anyExists || rowExists;
            allEmpty = allEmpty && !rowExists;
        }

        region.existence = anyExists
            ? TileExistenceState::Exists
            : (allEmpty ? TileExistenceState::Empty : TileExistenceState::Unknown);
        region.proofTree.existence = region.existence;
        region.proofTree.certainty = region.certainty;
        recordProofNode(
            previewKey,
            EvaluatedBox{
                .classification = TileClassification::Mixed,
                .interval = Interval{0.0, 1.0}
            },
            region.existence);
        return InequalityTileRefinementResult{
            .ok = true,
            .region = std::move(region),
            .visitedNodes = visitedNodes,
            .unknownPixels = unknownPixels,
            .intervalEvaluations = intervalEvaluations,
            .pointEvaluations = pointEvaluations
        };
    }

    [[nodiscard]] Interval axisRangeForPixel(const bool axisIsX,
                                             const uint32_t index,
                                             const double step) const
    {
        const auto lower = (axisIsX ? rootBounds.xMin : rootBounds.yMin)
            + static_cast<double>(index) * step;
        const auto upper = index == options.pixelsPerAxis - 1
            ? (axisIsX ? rootBounds.xMax : rootBounds.yMax)
            : lower + step;
        return {lower, upper};
    }

    [[nodiscard]] Interval tangentAxisArgumentRangeForPixel(const uint32_t index,
                                                            const double step) const
    {
        return scaleRange(
            axisRangeForPixel(tangentAxisCurvePlan->tangentAxisIsX, index, step),
            tangentAxisCurvePlan->tangentScale);
    }

    [[nodiscard]] std::optional<InequalityTileRefinementResult> runTangentAxisCurveIfEveryPixel()
    {
        if (!tangentAxisCurvePlan)
        {
            return std::nullopt;
        }

        ++visitedNodes;
        const auto pixelCount = options.pixelsPerAxis;
        const auto xStep = (rootBounds.xMax - rootBounds.xMin) / static_cast<double>(pixelCount);
        const auto yStep = (rootBounds.yMax - rootBounds.yMin) / static_cast<double>(pixelCount);
        const auto tangentStep = tangentAxisCurvePlan->tangentAxisIsX ? xStep : yStep;
        const auto otherStep = tangentAxisCurvePlan->tangentAxisIsX ? yStep : xStep;

        std::vector<Interval> tangentRanges(pixelCount);
        std::vector<uint8_t> tangentHasPole(pixelCount, uint8_t{0});
        for (uint32_t index = 0; index < pixelCount; ++index)
        {
            const auto argumentRange = tangentAxisArgumentRangeForPixel(index, tangentStep);
            tangentHasPole[index] = containsTangentPole(argumentRange) ? uint8_t{1} : uint8_t{0};
            tangentRanges[index] = tangentRange(argumentRange);
        }

        std::ranges::fill(region.pixels, PixelTrue);
        auto falsePixels = size_t{0};
        const auto markFalseColumn = [&](const uint32_t x, const uint32_t begin, const uint32_t end)
        {
            falsePixels += static_cast<size_t>(end - begin);
            for (auto y = begin; y < end; ++y)
            {
                region.pixels[static_cast<size_t>(y) * pixelCount + x] = PixelFalse;
            }
        };
        const auto markFalseRow = [&](const uint32_t y, const uint32_t begin, const uint32_t end)
        {
            falsePixels += static_cast<size_t>(end - begin);
            std::fill_n(
                region.pixels.data() + static_cast<size_t>(y) * pixelCount + begin,
                static_cast<size_t>(end - begin),
                PixelFalse);
        };
        const auto firstOtherMatching = [&](const bool otherAxisIsX,
                                            const double step,
                                            auto predicate)
        {
            return firstColumnMatching(0, pixelCount, [&](const uint32_t index)
            {
                return predicate(axisRangeForPixel(otherAxisIsX, index, step));
            });
        };

        if (tangentAxisCurvePlan->tangentAxisIsX)
        {
            for (uint32_t x = 0; x < pixelCount; ++x)
            {
                if (cancelled())
                {
                    return InequalityTileRefinementResult{
                        .ok = false,
                        .message = "Cancelled",
                        .visitedNodes = visitedNodes,
                        .unknownPixels = unknownPixels,
                        .intervalEvaluations = intervalEvaluations,
                        .pointEvaluations = pointEvaluations
                    };
                }

                if (tangentHasPole[x] == uint8_t{1})
                {
                    continue;
                }

                switch (tangentAxisCurvePlan->tangentComparison)
                {
                case FormulaOp::Less:
                    markFalseColumn(x, 0, firstOtherMatching(false, otherStep, [&](const Interval &other)
                    {
                        return other.upper > tangentRanges[x].lower;
                    }));
                    break;
                case FormulaOp::LessEqual:
                    markFalseColumn(x, 0, firstOtherMatching(false, otherStep, [&](const Interval &other)
                    {
                        return other.upper >= tangentRanges[x].lower;
                    }));
                    break;
                case FormulaOp::Greater:
                    markFalseColumn(x, firstOtherMatching(false, otherStep, [&](const Interval &other)
                    {
                        return other.lower >= tangentRanges[x].upper;
                    }), pixelCount);
                    break;
                case FormulaOp::GreaterEqual:
                    markFalseColumn(x, firstOtherMatching(false, otherStep, [&](const Interval &other)
                    {
                        return other.lower > tangentRanges[x].upper;
                    }), pixelCount);
                    break;
                default:
                    break;
                }
            }
        }
        else
        {
            for (uint32_t y = 0; y < pixelCount; ++y)
            {
                if (cancelled())
                {
                    return InequalityTileRefinementResult{
                        .ok = false,
                        .message = "Cancelled",
                        .visitedNodes = visitedNodes,
                        .unknownPixels = unknownPixels,
                        .intervalEvaluations = intervalEvaluations,
                        .pointEvaluations = pointEvaluations
                    };
                }

                if (tangentHasPole[y] == uint8_t{1})
                {
                    continue;
                }

                switch (tangentAxisCurvePlan->tangentComparison)
                {
                case FormulaOp::Less:
                    markFalseRow(y, 0, firstOtherMatching(true, otherStep, [&](const Interval &other)
                    {
                        return other.upper > tangentRanges[y].lower;
                    }));
                    break;
                case FormulaOp::LessEqual:
                    markFalseRow(y, 0, firstOtherMatching(true, otherStep, [&](const Interval &other)
                    {
                        return other.upper >= tangentRanges[y].lower;
                    }));
                    break;
                case FormulaOp::Greater:
                    markFalseRow(y, firstOtherMatching(true, otherStep, [&](const Interval &other)
                    {
                        return other.lower >= tangentRanges[y].upper;
                    }), pixelCount);
                    break;
                case FormulaOp::GreaterEqual:
                    markFalseRow(y, firstOtherMatching(true, otherStep, [&](const Interval &other)
                    {
                        return other.lower > tangentRanges[y].upper;
                    }), pixelCount);
                    break;
                default:
                    break;
                }
            }
        }

        const auto totalPixels = static_cast<size_t>(pixelCount) * pixelCount;
        region.existence = falsePixels < totalPixels
            ? TileExistenceState::Exists
            : TileExistenceState::Empty;
        region.proofTree.existence = region.existence;
        region.proofTree.certainty = region.certainty;
        recordProofNode(
            previewKey,
            EvaluatedBox{
                .classification = TileClassification::Mixed,
                .interval = Interval{0.0, 1.0}
            },
            region.existence);
        return InequalityTileRefinementResult{
            .ok = true,
            .region = std::move(region),
            .visitedNodes = visitedNodes,
            .unknownPixels = unknownPixels,
            .intervalEvaluations = intervalEvaluations,
            .pointEvaluations = pointEvaluations
        };
    }

    [[nodiscard]] static double minimumSquareForRange(const double lower, const double upper)
    {
        if (lower <= 0.0 && upper >= 0.0)
        {
            return 0.0;
        }
        const auto lowerMagnitude = std::abs(lower);
        const auto upperMagnitude = std::abs(upper);
        const auto minimumMagnitude = std::min(lowerMagnitude, upperMagnitude);
        return minimumMagnitude * minimumMagnitude;
    }

    [[nodiscard]] std::optional<InequalityTileRefinementResult> runSumSquaresDiskIfEveryPixel()
    {
        if (!sumSquaresDiskPlan || options.pixelsPerAxis < 64)
        {
            return std::nullopt;
        }
        if (sumSquaresDiskPlan->radiusSquared < 0.0
            || !supportedComparisonForCurve(sumSquaresDiskPlan->comparison))
        {
            return std::nullopt;
        }

        ++visitedNodes;
        const auto pixelCount = options.pixelsPerAxis;
        const auto xStep = (rootBounds.xMax - rootBounds.xMin) / static_cast<double>(pixelCount);
        const auto yStep = (rootBounds.yMax - rootBounds.yMin) / static_cast<double>(pixelCount);
        auto anyExists = false;
        auto allEmpty = true;

        for (uint32_t y = 0; y < pixelCount; ++y)
        {
            if (cancelled())
            {
                return InequalityTileRefinementResult{
                    .ok = false,
                    .message = "Cancelled",
                    .visitedNodes = visitedNodes,
                    .unknownPixels = unknownPixels,
                    .intervalEvaluations = intervalEvaluations,
                    .pointEvaluations = pointEvaluations
                };
            }

            auto *row = region.pixels.data() + static_cast<size_t>(y) * pixelCount;
            const auto yMin = rootBounds.yMin + static_cast<double>(y) * yStep;
            const auto yMax = y == pixelCount - 1
                ? rootBounds.yMax
                : rootBounds.yMin + static_cast<double>(y + 1) * yStep;

            if (sumSquaresDiskPlan->comparison == FormulaOp::Greater
                || sumSquaresDiskPlan->comparison == FormulaOp::GreaterEqual)
            {
                const auto maxYMagnitude = std::max(std::abs(yMin), std::abs(yMax));
                const auto remaining = sumSquaresDiskPlan->radiusSquared - maxYMagnitude * maxYMagnitude;
                if (remaining <= 0.0)
                {
                    std::fill_n(row, static_cast<size_t>(pixelCount), PixelTrue);
                    anyExists = true;
                    allEmpty = false;
                    continue;
                }

                const auto radius = std::sqrt(remaining);
                const auto falseBegin = sumSquaresDiskPlan->comparison == FormulaOp::Greater
                    ? firstColumnMatching(0, pixelCount, [&](const uint32_t x)
                    {
                        return xMinForColumn(x, xStep) >= -radius;
                    })
                    : firstColumnMatching(0, pixelCount, [&](const uint32_t x)
                    {
                        return xMinForColumn(x, xStep) > -radius;
                    });
                const auto falseEnd = sumSquaresDiskPlan->comparison == FormulaOp::Greater
                    ? firstColumnMatching(falseBegin, pixelCount, [&](const uint32_t x)
                    {
                        return xMaxForColumn(x, xStep) > radius;
                    })
                    : firstColumnMatching(falseBegin, pixelCount, [&](const uint32_t x)
                    {
                        return xMaxForColumn(x, xStep) >= radius;
                    });

                std::fill_n(row, static_cast<size_t>(falseBegin), PixelTrue);
                std::fill_n(row + falseBegin, static_cast<size_t>(falseEnd - falseBegin), PixelFalse);
                std::fill_n(row + falseEnd, static_cast<size_t>(pixelCount - falseEnd), PixelTrue);
                const auto rowExists = falseBegin > 0 || falseEnd < pixelCount;
                anyExists = anyExists || rowExists;
                allEmpty = allEmpty && !rowExists;
                continue;
            }

            const auto remaining = sumSquaresDiskPlan->radiusSquared - minimumSquareForRange(yMin, yMax);
            if (remaining < 0.0 || (remaining == 0.0 && sumSquaresDiskPlan->comparison == FormulaOp::Less))
            {
                std::fill_n(row, static_cast<size_t>(pixelCount), PixelFalse);
                continue;
            }
            const auto radius = std::sqrt(remaining);
            const auto trueBegin = sumSquaresDiskPlan->comparison == FormulaOp::Less
                ? firstColumnMatching(0, pixelCount, [&](const uint32_t x)
                {
                    return xMaxForColumn(x, xStep) > -radius;
                })
                : firstColumnMatching(0, pixelCount, [&](const uint32_t x)
                {
                    return xMaxForColumn(x, xStep) >= -radius;
                });
            const auto trueEnd = sumSquaresDiskPlan->comparison == FormulaOp::Less
                ? firstColumnMatching(trueBegin, pixelCount, [&](const uint32_t x)
                {
                    return xMinForColumn(x, xStep) >= radius;
                })
                : firstColumnMatching(trueBegin, pixelCount, [&](const uint32_t x)
                {
                    return xMinForColumn(x, xStep) > radius;
                });

            std::fill_n(row, static_cast<size_t>(trueBegin), PixelFalse);
            std::fill_n(row + trueBegin, static_cast<size_t>(trueEnd - trueBegin), PixelTrue);
            std::fill_n(row + trueEnd, static_cast<size_t>(pixelCount - trueEnd), PixelFalse);
            const auto rowExists = trueEnd > trueBegin;
            anyExists = anyExists || rowExists;
            allEmpty = allEmpty && !rowExists;
        }

        region.existence = anyExists
            ? TileExistenceState::Exists
            : (allEmpty ? TileExistenceState::Empty : TileExistenceState::Unknown);
        region.proofTree.existence = region.existence;
        region.proofTree.certainty = region.certainty;
        recordProofNode(
            previewKey,
            EvaluatedBox{
                .classification = TileClassification::Mixed,
                .interval = Interval{0.0, 1.0}
            },
            region.existence);
        return InequalityTileRefinementResult{
            .ok = true,
            .region = std::move(region),
            .visitedNodes = visitedNodes,
            .unknownPixels = unknownPixels,
            .intervalEvaluations = intervalEvaluations,
            .pointEvaluations = pointEvaluations
        };
    }

    [[nodiscard]] std::optional<InequalityTileRefinementResult> runSquareDifferenceBandIfEveryPixel()
    {
        if (!squareDifferenceBandPlan || options.pixelsPerAxis < 64)
        {
            return std::nullopt;
        }
        if (squareDifferenceBandPlan->widthSquared < 0.0
            || !supportedComparisonForCurve(squareDifferenceBandPlan->comparison))
        {
            return std::nullopt;
        }
        if (squareDifferenceBandPlan->widthSquared == 0.0
            && (squareDifferenceBandPlan->comparison == FormulaOp::Less
                || squareDifferenceBandPlan->comparison == FormulaOp::GreaterEqual))
        {
            return std::nullopt;
        }

        ++visitedNodes;
        const auto pixelCount = options.pixelsPerAxis;
        const auto xStep = (rootBounds.xMax - rootBounds.xMin) / static_cast<double>(pixelCount);
        const auto yStep = (rootBounds.yMax - rootBounds.yMin) / static_cast<double>(pixelCount);
        const auto width = std::sqrt(squareDifferenceBandPlan->widthSquared);
        auto anyExists = false;
        auto allEmpty = true;

        for (uint32_t y = 0; y < pixelCount; ++y)
        {
            if (cancelled())
            {
                return InequalityTileRefinementResult{
                    .ok = false,
                    .message = "Cancelled",
                    .visitedNodes = visitedNodes,
                    .unknownPixels = unknownPixels,
                    .intervalEvaluations = intervalEvaluations,
                    .pointEvaluations = pointEvaluations
                };
            }

            const auto yMin = rootBounds.yMin + static_cast<double>(y) * yStep;
            const auto yMax = y == pixelCount - 1
                ? rootBounds.yMax
                : rootBounds.yMin + static_cast<double>(y + 1) * yStep;
            auto *row = region.pixels.data() + static_cast<size_t>(y) * pixelCount;

            if (squareDifferenceBandPlan->comparison == FormulaOp::Greater
                || squareDifferenceBandPlan->comparison == FormulaOp::GreaterEqual)
            {
                const auto falseBegin = squareDifferenceBandPlan->comparison == FormulaOp::Greater
                    ? firstColumnMatching(0, pixelCount, [&](const uint32_t x)
                    {
                        return xMinForColumn(x, xStep) >= yMax - width;
                    })
                    : firstColumnMatching(0, pixelCount, [&](const uint32_t x)
                    {
                        return xMinForColumn(x, xStep) > yMax - width;
                    });
                const auto falseEnd = squareDifferenceBandPlan->comparison == FormulaOp::Greater
                    ? firstColumnMatching(falseBegin, pixelCount, [&](const uint32_t x)
                    {
                        return xMaxForColumn(x, xStep) > yMin + width;
                    })
                    : firstColumnMatching(falseBegin, pixelCount, [&](const uint32_t x)
                    {
                        return xMaxForColumn(x, xStep) >= yMin + width;
                    });

                std::fill_n(row, static_cast<size_t>(falseBegin), PixelTrue);
                std::fill_n(row + falseBegin, static_cast<size_t>(falseEnd - falseBegin), PixelFalse);
                std::fill_n(row + falseEnd, static_cast<size_t>(pixelCount - falseEnd), PixelTrue);
                const auto rowExists = falseBegin > 0 || falseEnd < pixelCount;
                anyExists = anyExists || rowExists;
                allEmpty = allEmpty && !rowExists;
                continue;
            }

            const auto trueBegin = squareDifferenceBandPlan->comparison == FormulaOp::Less
                ? firstColumnMatching(0, pixelCount, [&](const uint32_t x)
                {
                    return xMaxForColumn(x, xStep) > yMin - width;
                })
                : firstColumnMatching(0, pixelCount, [&](const uint32_t x)
                {
                    return xMaxForColumn(x, xStep) >= yMin - width;
                });
            const auto trueEnd = squareDifferenceBandPlan->comparison == FormulaOp::Less
                ? firstColumnMatching(trueBegin, pixelCount, [&](const uint32_t x)
                {
                    return xMinForColumn(x, xStep) >= yMax + width;
                })
                : firstColumnMatching(trueBegin, pixelCount, [&](const uint32_t x)
                {
                    return xMinForColumn(x, xStep) > yMax + width;
                });

            std::fill_n(row, static_cast<size_t>(trueBegin), PixelFalse);
            std::fill_n(row + trueBegin, static_cast<size_t>(trueEnd - trueBegin), PixelTrue);
            std::fill_n(row + trueEnd, static_cast<size_t>(pixelCount - trueEnd), PixelFalse);
            const auto rowExists = trueEnd > trueBegin;
            anyExists = anyExists || rowExists;
            allEmpty = allEmpty && !rowExists;
        }

        region.existence = anyExists
            ? TileExistenceState::Exists
            : (allEmpty ? TileExistenceState::Empty : TileExistenceState::Unknown);
        region.proofTree.existence = region.existence;
        region.proofTree.certainty = region.certainty;
        recordProofNode(
            previewKey,
            EvaluatedBox{
                .classification = TileClassification::Mixed,
                .interval = Interval{0.0, 1.0}
            },
            region.existence);
        return InequalityTileRefinementResult{
            .ok = true,
            .region = std::move(region),
            .visitedNodes = visitedNodes,
            .unknownPixels = unknownPixels,
            .intervalEvaluations = intervalEvaluations,
            .pointEvaluations = pointEvaluations
        };
    }

    [[nodiscard]] std::optional<InequalityTileRefinementResult> runTangentPoleIfEveryPixel(
        const TangentPoleInequality &tangent)
    {
        ++visitedNodes;
        const auto argumentAffine = AffineInequality{
            .xCoefficient = tangent.argumentXCoefficient,
            .yCoefficient = tangent.argumentYCoefficient,
            .constant = tangent.argumentConstant,
            .comparison = FormulaOp::LessEqual
        };
        const auto rootArgumentRange = affineValueRange(argumentAffine, rootBounds);
        if (!containsTangentPole(rootArgumentRange))
        {
            --visitedNodes;
            return std::nullopt;
        }

        const auto xStep = (rootBounds.xMax - rootBounds.xMin)
            / static_cast<double>(options.pixelsPerAxis);
        const auto yStep = (rootBounds.yMax - rootBounds.yMin)
            / static_cast<double>(options.pixelsPerAxis);
        const auto pixelArgumentSpan = std::abs(tangent.argumentXCoefficient) * xStep
            + std::abs(tangent.argumentYCoefficient) * yStep;
        if (pixelArgumentSpan <= std::numbers::pi)
        {
            for (uint32_t y = 0; y < options.pixelsPerAxis; ++y)
            {
                if (cancelled())
                {
                    return InequalityTileRefinementResult{
                        .ok = false,
                        .message = "Cancelled",
                        .visitedNodes = visitedNodes,
                        .unknownPixels = unknownPixels,
                        .intervalEvaluations = intervalEvaluations,
                        .pointEvaluations = pointEvaluations
                    };
                }

                const auto yMin = rootBounds.yMin + static_cast<double>(y) * yStep;
                const auto yMax = y == options.pixelsPerAxis - 1
                    ? rootBounds.yMax
                    : yMin + yStep;
                for (uint32_t x = 0; x < options.pixelsPerAxis; ++x)
                {
                    const auto xMin = rootBounds.xMin + static_cast<double>(x) * xStep;
                    const auto xMax = x == options.pixelsPerAxis - 1
                        ? rootBounds.xMax
                        : xMin + xStep;
                    const auto pixelArgumentRange = affineValueRange(
                        argumentAffine,
                        Rect{xMin, xMax, yMin, yMax});
                    if (!containsTangentPole(pixelArgumentRange))
                    {
                        --visitedNodes;
                        return std::nullopt;
                    }
                }
            }
        }

        std::ranges::fill(region.pixels, PixelTrue);
        region.existence = TileExistenceState::Exists;
        region.proofTree.existence = TileExistenceState::Exists;
        region.proofTree.certainty = region.certainty;
        recordProofNode(
            previewKey,
            EvaluatedBox{
                .classification = TileClassification::Mixed,
                .interval = Interval{0.0, 1.0, true}
            },
            TileExistenceState::Exists);

        return InequalityTileRefinementResult{
            .ok = true,
            .region = std::move(region),
            .visitedNodes = visitedNodes,
            .unknownPixels = unknownPixels,
            .intervalEvaluations = intervalEvaluations,
            .pointEvaluations = pointEvaluations
        };
    }

    [[nodiscard]] static EvaluatedBox evaluatedBoxFor(const Interval &interval)
    {
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

    [[nodiscard]] bool sliceUsesOnlyAxis(const FormulaBytecodeSlice &slice, const std::optional<size_t> axisSlot) const
    {
        if (slice.variableMask == std::numeric_limits<uint64_t>::max())
        {
            return false;
        }
        auto allowedMask = uint64_t{0};
        if (axisSlot)
        {
            if (*axisSlot >= std::numeric_limits<uint64_t>::digits)
            {
                return false;
            }
            allowedMask = uint64_t{1} << *axisSlot;
        }
        return (slice.variableMask & ~allowedMask) == 0;
    }

    [[nodiscard]] bool sliceHasExpensiveOperation(const FormulaBytecodeSlice &slice) const
    {
        if (slice.offset > formula.evaluationIr.size() || slice.count > formula.evaluationIr.size() - slice.offset)
        {
            return false;
        }

        for (const auto &instruction : std::span{formula.evaluationIr}.subspan(slice.offset, slice.count))
        {
            switch (instruction.op)
            {
            case FormulaOp::Divide:
            case FormulaOp::Power:
            case FormulaOp::Sin:
            case FormulaOp::Cos:
            case FormulaOp::Tan:
            case FormulaOp::Log:
            case FormulaOp::Exp:
            case FormulaOp::Sqrt:
                return true;
            default:
                break;
            }
        }
        return false;
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

    [[nodiscard]] static bool supportedDirectUnaryOp(const FormulaOp op)
    {
        return op == FormulaOp::Sin
            || op == FormulaOp::Tan
            || op == FormulaOp::Log
            || op == FormulaOp::Exp
            || op == FormulaOp::Sqrt;
    }

    [[nodiscard]] static Interval applyDirectUnaryInterval(const FormulaOp op, const Interval &value)
    {
        switch (op)
        {
        case FormulaOp::Sin:
            return sineRange(value);
        case FormulaOp::Tan:
            return tangentRange(value);
        case FormulaOp::Log:
            return logRange(value);
        case FormulaOp::Exp:
            return expRange(value);
        case FormulaOp::Sqrt:
            return sqrtRange(value);
        default:
            return {1.0, 0.0, true};
        }
    }

    [[nodiscard]] static double applyDirectUnaryDouble(const FormulaOp op, const double value)
    {
        switch (op)
        {
        case FormulaOp::Sin:
            return std::sin(value);
        case FormulaOp::Tan:
            return std::tan(value);
        case FormulaOp::Log:
            return std::log(value);
        case FormulaOp::Exp:
            return std::exp(value);
        case FormulaOp::Sqrt:
            return std::sqrt(value);
        default:
            return std::numeric_limits<double>::quiet_NaN();
        }
    }

    [[nodiscard]] bool isXVariable(const FormulaInstruction &instruction) const
    {
        return xSlot && instruction.op == FormulaOp::PushVariable && instruction.variableSlot == *xSlot;
    }

    [[nodiscard]] bool isYVariable(const FormulaInstruction &instruction) const
    {
        return ySlot && instruction.op == FormulaOp::PushVariable && instruction.variableSlot == *ySlot;
    }

    [[nodiscard]] bool isXOrYVariable(const FormulaInstruction &instruction) const
    {
        return isXVariable(instruction) || isYVariable(instruction);
    }

    [[nodiscard]] static bool isSquarePower(const std::span<const FormulaInstruction> instructions,
                                            const size_t offset)
    {
        return offset + 2 < instructions.size()
            && instructions[offset + 1].op == FormulaOp::PushConstant
            && instructions[offset + 1].constant == 2.0
            && instructions[offset + 2].op == FormulaOp::Power;
    }

    [[nodiscard]] OperandPlan operandPlanFor(const FormulaBytecodeSlice &slice) const
    {
        auto plan = OperandPlan{.slice = slice};
        if (slice.offset > formula.evaluationIr.size() || slice.count > formula.evaluationIr.size() - slice.offset)
        {
            return plan;
        }

        const auto instructions = std::span{formula.evaluationIr}.subspan(slice.offset, slice.count);
        if (instructions.size() == 1)
        {
            const auto &instruction = instructions.front();
            if (instruction.op == FormulaOp::PushConstant)
            {
                plan.kind = DirectOperandKind::Constant;
                plan.constant = instruction.constant;
            }
            else if (instruction.op == FormulaOp::PushVariable)
            {
                if (xSlot && instruction.variableSlot == *xSlot)
                {
                    plan.kind = DirectOperandKind::XVariable;
                }
                else if (ySlot && instruction.variableSlot == *ySlot)
                {
                    plan.kind = DirectOperandKind::YVariable;
                }
            }
            return plan;
        }

        if (instructions.size() >= 2
            && instructions.size() <= plan.unaryOps.size() + 1
            && instructions.front().op == FormulaOp::PushVariable)
        {
            auto allSupported = true;
            for (auto index = size_t{1}; index < instructions.size(); ++index)
            {
                if (!supportedDirectUnaryOp(instructions[index].op))
                {
                    allSupported = false;
                    break;
                }
            }

            if (allSupported)
            {
                plan.kind = DirectOperandKind::UnaryVariable;
                plan.constant = 1.0;
                plan.variableSlot = instructions.front().variableSlot;
                plan.unaryCount = instructions.size() - 1;
                for (auto index = size_t{0}; index < plan.unaryCount; ++index)
                {
                    plan.unaryOps[index] = instructions[index + 1].op;
                }
                return plan;
            }
        }

        if (instructions.size() >= 4
            && instructions.size() <= plan.unaryOps.size() + 3
            && instructions[2].op == FormulaOp::Multiply)
        {
            const auto constantFirst = instructions[0].op == FormulaOp::PushConstant
                && instructions[1].op == FormulaOp::PushVariable;
            const auto variableFirst = instructions[0].op == FormulaOp::PushVariable
                && instructions[1].op == FormulaOp::PushConstant;
            if (constantFirst || variableFirst)
            {
                auto allSupported = true;
                for (auto index = size_t{3}; index < instructions.size(); ++index)
                {
                    if (!supportedDirectUnaryOp(instructions[index].op))
                    {
                        allSupported = false;
                        break;
                    }
                }

                if (allSupported)
                {
                    plan.kind = DirectOperandKind::UnaryVariable;
                    plan.constant = constantFirst ? instructions[0].constant : instructions[1].constant;
                    plan.variableSlot = constantFirst ? instructions[1].variableSlot : instructions[0].variableSlot;
                    plan.unaryCount = instructions.size() - 3;
                    for (auto index = size_t{0}; index < plan.unaryCount; ++index)
                    {
                        plan.unaryOps[index] = instructions[index + 3].op;
                    }
                    return plan;
                }
            }
        }

        if (xSlot && ySlot
            && instructions.size() == 4
            && instructions[0].op == FormulaOp::PushVariable
            && instructions[1].op == FormulaOp::PushVariable
            && instructions[2].op == FormulaOp::Multiply
            && instructions[3].op == FormulaOp::Sin
            && ((instructions[0].variableSlot == *xSlot && instructions[1].variableSlot == *ySlot)
                || (instructions[0].variableSlot == *ySlot && instructions[1].variableSlot == *xSlot)))
        {
            plan.kind = DirectOperandKind::SinProductXY;
            plan.constant = 1.0;
        }
        else if (xSlot && ySlot
            && instructions.size() == 6
            && instructions[5].op == FormulaOp::Sin)
        {
            if (instructions[0].op == FormulaOp::PushConstant
                && isXOrYVariable(instructions[1])
                && instructions[2].op == FormulaOp::Multiply
                && isXOrYVariable(instructions[3])
                && instructions[1].variableSlot != instructions[3].variableSlot
                && instructions[4].op == FormulaOp::Multiply)
            {
                plan.kind = DirectOperandKind::SinProductXY;
                plan.constant = instructions[0].constant;
            }
            else if (isXOrYVariable(instructions[0])
                && instructions[1].op == FormulaOp::PushConstant
                && instructions[2].op == FormulaOp::Multiply
                && isXOrYVariable(instructions[3])
                && instructions[0].variableSlot != instructions[3].variableSlot
                && instructions[4].op == FormulaOp::Multiply)
            {
                plan.kind = DirectOperandKind::SinProductXY;
                plan.constant = instructions[1].constant;
            }
            else if (isXOrYVariable(instructions[0])
                && isXOrYVariable(instructions[1])
                && instructions[0].variableSlot != instructions[1].variableSlot
                && instructions[2].op == FormulaOp::Multiply
                && instructions[3].op == FormulaOp::PushConstant
                && instructions[4].op == FormulaOp::Multiply)
            {
                plan.kind = DirectOperandKind::SinProductXY;
                plan.constant = instructions[3].constant;
            }
        }

        if (xSlot && ySlot
            && instructions.size() == 4
            && isXVariable(instructions[0])
            && supportedDirectUnaryOp(instructions[1].op)
            && isYVariable(instructions[2])
            && instructions[3].op == FormulaOp::Subtract)
        {
            plan.kind = DirectOperandKind::UnaryXMinusY;
            plan.unaryOps[0] = instructions[1].op;
            plan.unaryCount = 1;
        }
        else if (xSlot && ySlot
            && instructions.size() == 4
            && isYVariable(instructions[0])
            && isXVariable(instructions[1])
            && supportedDirectUnaryOp(instructions[2].op)
            && instructions[3].op == FormulaOp::Subtract)
        {
            plan.kind = DirectOperandKind::YMinusUnaryX;
            plan.unaryOps[0] = instructions[2].op;
            plan.unaryCount = 1;
        }
        else if (xSlot
            && instructions.size() == 5
            && instructions[0].op == FormulaOp::PushConstant
            && isXVariable(instructions[1])
            && instructions[2].op == FormulaOp::PushConstant
            && instructions[3].op == FormulaOp::Subtract
            && instructions[4].op == FormulaOp::Divide)
        {
            plan.kind = DirectOperandKind::ReciprocalShiftX;
            plan.constant = instructions[0].constant;
            plan.secondaryConstant = instructions[2].constant;
        }

        if (xSlot && ySlot
            && instructions.size() == 5
            && isXOrYVariable(instructions[0])
            && isXOrYVariable(instructions[1])
            && instructions[0].variableSlot != instructions[1].variableSlot
            && instructions[2].op == FormulaOp::Subtract
            && instructions[3].op == FormulaOp::PushConstant
            && instructions[3].constant == 2.0
            && instructions[4].op == FormulaOp::Power)
        {
            plan.kind = DirectOperandKind::SquareDifferenceXY;
        }
        else if (xSlot && ySlot
            && instructions.size() == 7
            && isXOrYVariable(instructions[0])
            && isSquarePower(instructions, 0)
            && isXOrYVariable(instructions[3])
            && instructions[0].variableSlot != instructions[3].variableSlot
            && isSquarePower(instructions, 3)
            && instructions[6].op == FormulaOp::Add)
        {
            plan.kind = DirectOperandKind::SumSquaresXY;
        }

        return plan;
    }

    [[nodiscard]] static Interval productRange(const Interval &lhs, const Interval &rhs)
    {
        if (lhs.undefined() || rhs.undefined())
        {
            return {1.0, 0.0, lhs.hasDiscontinuity() || rhs.hasDiscontinuity()};
        }

        const auto a = lhs.lower * rhs.lower;
        const auto b = lhs.lower * rhs.upper;
        const auto c = lhs.upper * rhs.lower;
        const auto d = lhs.upper * rhs.upper;
        return {
            std::min(std::min(a, b), std::min(c, d)),
            std::max(std::max(a, b), std::max(c, d)),
            lhs.hasDiscontinuity() || rhs.hasDiscontinuity()
        };
    }

    [[nodiscard]] static Interval scaleRange(const Interval &value, const double scale)
    {
        if (value.undefined())
        {
            return {1.0, 0.0, value.hasDiscontinuity()};
        }
        const auto lower = value.lower * scale;
        const auto upper = value.upper * scale;
        return {
            std::min(lower, upper),
            std::max(lower, upper),
            value.hasDiscontinuity()
        };
    }

    [[nodiscard]] static Interval squareRange(const Interval &value)
    {
        if (value.undefined())
        {
            return {1.0, 0.0, value.hasDiscontinuity()};
        }

        const auto lowerMagnitude = std::abs(value.lower);
        const auto upperMagnitude = std::abs(value.upper);
        const auto upper = std::max(lowerMagnitude, upperMagnitude);
        if (value.contains(0.0))
        {
            return {0.0, upper * upper, value.hasDiscontinuity()};
        }

        const auto lower = std::min(lowerMagnitude, upperMagnitude);
        return {lower * lower, upper * upper, value.hasDiscontinuity()};
    }

    [[nodiscard]] static Interval differenceRange(const Interval &lhs, const Interval &rhs)
    {
        if (lhs.undefined() || rhs.undefined())
        {
            return {1.0, 0.0, lhs.hasDiscontinuity() || rhs.hasDiscontinuity()};
        }
        return {
            lhs.lower - rhs.upper,
            lhs.upper - rhs.lower,
            lhs.hasDiscontinuity() || rhs.hasDiscontinuity()
        };
    }

    [[nodiscard]] static Interval reciprocalShiftRange(const Interval &xRange,
                                                       const double numerator,
                                                       const double shift)
    {
        const auto denominator = differenceRange(xRange, Interval{shift});
        if (denominator.undefined())
        {
            return denominator;
        }
        if (denominator.contains(0.0))
        {
            return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), true};
        }

        const auto lower = numerator / denominator.lower;
        const auto upper = numerator / denominator.upper;
        return {
            std::min(lower, upper),
            std::max(lower, upper),
            denominator.hasDiscontinuity()
        };
    }

    void setIntervalVariables(const Interval &xRange, const Interval &yRange)
    {
        if (xSlot)
        {
            intervalVariables[*xSlot] = xRange;
        }
        if (ySlot)
        {
            intervalVariables[*ySlot] = yRange;
        }
    }

    void setDoubleVariables(const double x, const double y)
    {
        if (xSlot)
        {
            doubleVariables[*xSlot] = x;
        }
        if (ySlot)
        {
            doubleVariables[*ySlot] = y;
        }
    }

    [[nodiscard]] std::optional<Interval> directIntervalVariableForSlot(
        const size_t slot,
        const Interval &xRange,
        const Interval &yRange) const
    {
        if (xSlot && slot == *xSlot)
        {
            return xRange;
        }
        if (ySlot && slot == *ySlot)
        {
            return yRange;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<double> directDoubleVariableForSlot(
        const size_t slot,
        const double x,
        const double y) const
    {
        if (xSlot && slot == *xSlot)
        {
            return x;
        }
        if (ySlot && slot == *ySlot)
        {
            return y;
        }
        return std::nullopt;
    }

    [[nodiscard]] Interval evaluateOperandInterval(const OperandPlan &plan)
    {
        switch (plan.kind)
        {
        case DirectOperandKind::Constant:
            return Interval{plan.constant};
        case DirectOperandKind::XVariable:
            return intervalVariables[*xSlot];
        case DirectOperandKind::YVariable:
            return intervalVariables[*ySlot];
        case DirectOperandKind::UnaryVariable:
        {
            auto value = scaleRange(intervalVariables[plan.variableSlot], plan.constant);
            for (auto index = size_t{0}; index < plan.unaryCount; ++index)
            {
                value = applyDirectUnaryInterval(plan.unaryOps[index], value);
            }
            return value;
        }
        case DirectOperandKind::SinProductXY:
            return sineRange(scaleRange(
                productRange(intervalVariables[*xSlot], intervalVariables[*ySlot]),
                plan.constant));
        case DirectOperandKind::SquareDifferenceXY:
            return squareRange(differenceRange(intervalVariables[*xSlot], intervalVariables[*ySlot]));
        case DirectOperandKind::SumSquaresXY:
        {
            const auto xSquared = squareRange(intervalVariables[*xSlot]);
            const auto ySquared = squareRange(intervalVariables[*ySlot]);
            if (xSquared.undefined() || ySquared.undefined())
            {
                return {1.0, 0.0, xSquared.hasDiscontinuity() || ySquared.hasDiscontinuity()};
            }
            return {
                xSquared.lower + ySquared.lower,
                xSquared.upper + ySquared.upper,
                xSquared.hasDiscontinuity() || ySquared.hasDiscontinuity()
            };
        }
        case DirectOperandKind::UnaryXMinusY:
        {
            auto xValue = intervalVariables[*xSlot];
            for (auto index = size_t{0}; index < plan.unaryCount; ++index)
            {
                xValue = applyDirectUnaryInterval(plan.unaryOps[index], xValue);
            }
            return differenceRange(xValue, intervalVariables[*ySlot]);
        }
        case DirectOperandKind::YMinusUnaryX:
        {
            auto xValue = intervalVariables[*xSlot];
            for (auto index = size_t{0}; index < plan.unaryCount; ++index)
            {
                xValue = applyDirectUnaryInterval(plan.unaryOps[index], xValue);
            }
            return differenceRange(intervalVariables[*ySlot], xValue);
        }
        case DirectOperandKind::ReciprocalShiftX:
            return reciprocalShiftRange(intervalVariables[*xSlot], plan.constant, plan.secondaryConstant);
        case DirectOperandKind::Bytecode:
            break;
        }

        return formula.evaluateIntervalBytecode(plan.slice, intervalVariables, evaluationStack);
    }

    [[nodiscard]] Interval evaluateOperandInterval(const OperandPlan &plan,
                                                   const Interval &xRange,
                                                   const Interval &yRange)
    {
        switch (plan.kind)
        {
        case DirectOperandKind::Constant:
            return Interval{plan.constant};
        case DirectOperandKind::XVariable:
            return xRange;
        case DirectOperandKind::YVariable:
            return yRange;
        case DirectOperandKind::UnaryVariable:
        {
            auto maybeValue = directIntervalVariableForSlot(plan.variableSlot, xRange, yRange);
            if (!maybeValue)
            {
                break;
            }

            auto value = *maybeValue;
            value = scaleRange(value, plan.constant);
            for (auto index = size_t{0}; index < plan.unaryCount; ++index)
            {
                value = applyDirectUnaryInterval(plan.unaryOps[index], value);
            }
            return value;
        }
        case DirectOperandKind::SinProductXY:
            return sineRange(scaleRange(productRange(xRange, yRange), plan.constant));
        case DirectOperandKind::SquareDifferenceXY:
            return squareRange(differenceRange(xRange, yRange));
        case DirectOperandKind::SumSquaresXY:
        {
            const auto xSquared = squareRange(xRange);
            const auto ySquared = squareRange(yRange);
            if (xSquared.undefined() || ySquared.undefined())
            {
                return {1.0, 0.0, xSquared.hasDiscontinuity() || ySquared.hasDiscontinuity()};
            }
            return {
                xSquared.lower + ySquared.lower,
                xSquared.upper + ySquared.upper,
                xSquared.hasDiscontinuity() || ySquared.hasDiscontinuity()
            };
        }
        case DirectOperandKind::UnaryXMinusY:
        {
            auto xValue = xRange;
            for (auto index = size_t{0}; index < plan.unaryCount; ++index)
            {
                xValue = applyDirectUnaryInterval(plan.unaryOps[index], xValue);
            }
            return differenceRange(xValue, yRange);
        }
        case DirectOperandKind::YMinusUnaryX:
        {
            auto xValue = xRange;
            for (auto index = size_t{0}; index < plan.unaryCount; ++index)
            {
                xValue = applyDirectUnaryInterval(plan.unaryOps[index], xValue);
            }
            return differenceRange(yRange, xValue);
        }
        case DirectOperandKind::ReciprocalShiftX:
            return reciprocalShiftRange(xRange, plan.constant, plan.secondaryConstant);
        case DirectOperandKind::Bytecode:
            break;
        }

        setIntervalVariables(xRange, yRange);
        return evaluateOperandInterval(plan);
    }

    [[nodiscard]] double evaluateOperandDouble(const OperandPlan &plan)
    {
        switch (plan.kind)
        {
        case DirectOperandKind::Constant:
            return plan.constant;
        case DirectOperandKind::XVariable:
            return doubleVariables[*xSlot];
        case DirectOperandKind::YVariable:
            return doubleVariables[*ySlot];
        case DirectOperandKind::UnaryVariable:
        {
            auto value = doubleVariables[plan.variableSlot] * plan.constant;
            for (auto index = size_t{0}; index < plan.unaryCount; ++index)
            {
                value = applyDirectUnaryDouble(plan.unaryOps[index], value);
            }
            return value;
        }
        case DirectOperandKind::SinProductXY:
            return std::sin(plan.constant * doubleVariables[*xSlot] * doubleVariables[*ySlot]);
        case DirectOperandKind::SquareDifferenceXY:
        {
            const auto difference = doubleVariables[*xSlot] - doubleVariables[*ySlot];
            return difference * difference;
        }
        case DirectOperandKind::SumSquaresXY:
            return doubleVariables[*xSlot] * doubleVariables[*xSlot]
                + doubleVariables[*ySlot] * doubleVariables[*ySlot];
        case DirectOperandKind::UnaryXMinusY:
        {
            auto xValue = doubleVariables[*xSlot];
            for (auto index = size_t{0}; index < plan.unaryCount; ++index)
            {
                xValue = applyDirectUnaryDouble(plan.unaryOps[index], xValue);
            }
            return xValue - doubleVariables[*ySlot];
        }
        case DirectOperandKind::YMinusUnaryX:
        {
            auto xValue = doubleVariables[*xSlot];
            for (auto index = size_t{0}; index < plan.unaryCount; ++index)
            {
                xValue = applyDirectUnaryDouble(plan.unaryOps[index], xValue);
            }
            return doubleVariables[*ySlot] - xValue;
        }
        case DirectOperandKind::ReciprocalShiftX:
            return plan.constant / (doubleVariables[*xSlot] - plan.secondaryConstant);
        case DirectOperandKind::Bytecode:
            break;
        }

        return formula.evaluateDoubleBytecode(plan.slice, doubleVariables, doubleEvaluationStack);
    }

    [[nodiscard]] double evaluateOperandDouble(const OperandPlan &plan, const double x, const double y)
    {
        switch (plan.kind)
        {
        case DirectOperandKind::Constant:
            return plan.constant;
        case DirectOperandKind::XVariable:
            return x;
        case DirectOperandKind::YVariable:
            return y;
        case DirectOperandKind::UnaryVariable:
        {
            auto maybeValue = directDoubleVariableForSlot(plan.variableSlot, x, y);
            if (!maybeValue)
            {
                break;
            }

            auto value = *maybeValue;
            value *= plan.constant;
            for (auto index = size_t{0}; index < plan.unaryCount; ++index)
            {
                value = applyDirectUnaryDouble(plan.unaryOps[index], value);
            }
            return value;
        }
        case DirectOperandKind::SinProductXY:
            return std::sin(plan.constant * x * y);
        case DirectOperandKind::SquareDifferenceXY:
        {
            const auto difference = x - y;
            return difference * difference;
        }
        case DirectOperandKind::SumSquaresXY:
            return x * x + y * y;
        case DirectOperandKind::UnaryXMinusY:
        {
            auto xValue = x;
            for (auto index = size_t{0}; index < plan.unaryCount; ++index)
            {
                xValue = applyDirectUnaryDouble(plan.unaryOps[index], xValue);
            }
            return xValue - y;
        }
        case DirectOperandKind::YMinusUnaryX:
        {
            auto xValue = x;
            for (auto index = size_t{0}; index < plan.unaryCount; ++index)
            {
                xValue = applyDirectUnaryDouble(plan.unaryOps[index], xValue);
            }
            return y - xValue;
        }
        case DirectOperandKind::ReciprocalShiftX:
            return plan.constant / (x - plan.secondaryConstant);
        case DirectOperandKind::Bytecode:
            break;
        }

        setDoubleVariables(x, y);
        return evaluateOperandDouble(plan);
    }

    void initializeRootAxisOnlyOperandCache()
    {
        if (!formula.rootComparisonBytecode)
        {
            return;
        }

        const auto &root = *formula.rootComparisonBytecode;
        if (sliceUsesOnlyAxis(root.rhs, ySlot) && sliceHasExpensiveOperation(root.rhs))
        {
            rootAxisOnlyOperandCache = RootAxisOnlyOperandCache{
                .axisIsY = true,
                .cachedOperandIsLhs = false,
                .cachedOperand = operandPlanFor(root.rhs),
                .otherOperand = operandPlanFor(root.lhs)
            };
        }
        else if (sliceUsesOnlyAxis(root.lhs, ySlot) && sliceHasExpensiveOperation(root.lhs))
        {
            rootAxisOnlyOperandCache = RootAxisOnlyOperandCache{
                .axisIsY = true,
                .cachedOperandIsLhs = true,
                .cachedOperand = operandPlanFor(root.lhs),
                .otherOperand = operandPlanFor(root.rhs)
            };
        }
        else if (sliceUsesOnlyAxis(root.rhs, xSlot) && sliceHasExpensiveOperation(root.rhs))
        {
            rootAxisOnlyOperandCache = RootAxisOnlyOperandCache{
                .axisIsY = false,
                .cachedOperandIsLhs = false,
                .cachedOperand = operandPlanFor(root.rhs),
                .otherOperand = operandPlanFor(root.lhs)
            };
        }
        else if (sliceUsesOnlyAxis(root.lhs, xSlot) && sliceHasExpensiveOperation(root.lhs))
        {
            rootAxisOnlyOperandCache = RootAxisOnlyOperandCache{
                .axisIsY = false,
                .cachedOperandIsLhs = true,
                .cachedOperand = operandPlanFor(root.lhs),
                .otherOperand = operandPlanFor(root.rhs)
            };
        }

        if (rootAxisOnlyOperandCache)
        {
            initializeRootAxisCacheAddresses();
        }
    }

    void initializeRootAxisCacheAddresses()
    {
        const auto previewIndex = rootAxisOnlyOperandCache->axisIsY ? previewKey.y : previewKey.x;
        for (auto shift = 0; shift <= RootAxisOnlyOperandCache::MaxCachedAxisShift; ++shift)
        {
            const auto scale = int64_t{1} << shift;
            rootAxisOnlyOperandCache->scaleByShift[static_cast<size_t>(shift)] = scale;
            if ((previewIndex > 0 && previewIndex > std::numeric_limits<int64_t>::max() / scale)
                || (previewIndex < 0 && previewIndex < std::numeric_limits<int64_t>::min() / scale))
            {
                rootAxisOnlyOperandCache->addressValid[static_cast<size_t>(shift)] = uint8_t{0};
                continue;
            }

            rootAxisOnlyOperandCache->baseIndexByShift[static_cast<size_t>(shift)] = previewIndex * scale;
            rootAxisOnlyOperandCache->addressValid[static_cast<size_t>(shift)] = uint8_t{1};
        }
    }

    void initializeRootComparisonDirectPlan()
    {
        if (rootAxisOnlyOperandCache || !formula.rootComparisonBytecode)
        {
            return;
        }

        const auto &root = *formula.rootComparisonBytecode;
        auto lhs = operandPlanFor(root.lhs);
        auto rhs = operandPlanFor(root.rhs);
        if (lhs.kind == DirectOperandKind::Bytecode && rhs.kind == DirectOperandKind::Bytecode)
        {
            return;
        }

        rootComparisonDirectPlan = RootComparisonDirectPlan{
            .comparison = root.comparison,
            .lhs = lhs,
            .rhs = rhs
        };
    }

    void initializeSinProductAxisComparisonPlan()
    {
        if (!rootAxisOnlyOperandCache || !formula.rootComparisonBytecode)
        {
            return;
        }

        const auto comparison = formula.rootComparisonBytecode->comparison;
        if (rootAxisOnlyOperandCache->cachedOperandIsLhs
            && rootAxisOnlyOperandCache->otherOperand.kind == DirectOperandKind::SinProductXY)
        {
            sinProductAxisComparisonPlan = SinProductAxisComparisonPlan{
                .sineScale = rootAxisOnlyOperandCache->otherOperand.constant,
                .sineComparison = invertedComparison(comparison)
            };
            return;
        }

        if (!rootAxisOnlyOperandCache->cachedOperandIsLhs
            && rootAxisOnlyOperandCache->otherOperand.kind == DirectOperandKind::SinProductXY)
        {
            sinProductAxisComparisonPlan = SinProductAxisComparisonPlan{
                .sineScale = rootAxisOnlyOperandCache->otherOperand.constant,
                .sineComparison = comparison
            };
        }
    }

    void initializeSumSquaresDiskPlan()
    {
        if (!formula.rootComparisonBytecode)
        {
            return;
        }

        const auto &root = *formula.rootComparisonBytecode;
        const auto lhs = operandPlanFor(root.lhs);
        const auto rhs = operandPlanFor(root.rhs);
        const auto assignPlan = [this](const OperandPlan &constant, const FormulaOp comparison)
        {
            if (supportedComparisonForCurve(comparison))
            {
                sumSquaresDiskPlan = SumSquaresDiskPlan{
                    .radiusSquared = constant.constant,
                    .comparison = comparison
                };
            }
        };

        if (lhs.kind == DirectOperandKind::SumSquaresXY && rhs.kind == DirectOperandKind::Constant)
        {
            assignPlan(rhs, root.comparison);
            return;
        }
        if (lhs.kind == DirectOperandKind::Constant && rhs.kind == DirectOperandKind::SumSquaresXY)
        {
            assignPlan(lhs, invertedComparison(root.comparison));
        }
    }

    void initializeSquareDifferenceBandPlan()
    {
        if (!formula.rootComparisonBytecode)
        {
            return;
        }

        const auto &root = *formula.rootComparisonBytecode;
        const auto lhs = operandPlanFor(root.lhs);
        const auto rhs = operandPlanFor(root.rhs);
        const auto assignPlan = [this](const OperandPlan &constant, const FormulaOp comparison)
        {
            if (supportedComparisonForCurve(comparison))
            {
                squareDifferenceBandPlan = SquareDifferenceBandPlan{
                    .widthSquared = constant.constant,
                    .comparison = comparison
                };
            }
        };

        if (lhs.kind == DirectOperandKind::SquareDifferenceXY && rhs.kind == DirectOperandKind::Constant)
        {
            assignPlan(rhs, root.comparison);
            return;
        }
        if (lhs.kind == DirectOperandKind::Constant && rhs.kind == DirectOperandKind::SquareDifferenceXY)
        {
            assignPlan(lhs, invertedComparison(root.comparison));
        }
    }

    void initializeReciprocalCurvePlan()
    {
        if (!formula.rootComparisonBytecode || !ySlot)
        {
            return;
        }

        const auto &root = *formula.rootComparisonBytecode;
        const auto lhs = operandPlanFor(root.lhs);
        const auto rhs = operandPlanFor(root.rhs);
        const auto assignPlan = [this](const OperandPlan &reciprocal, const FormulaOp yComparison)
        {
            if (supportedComparisonForCurve(yComparison))
            {
                reciprocalCurvePlan = ReciprocalCurvePlan{
                    .numerator = reciprocal.constant,
                    .shift = reciprocal.secondaryConstant,
                    .yComparison = yComparison
                };
            }
        };

        if (lhs.kind == DirectOperandKind::YVariable && rhs.kind == DirectOperandKind::ReciprocalShiftX)
        {
            assignPlan(rhs, root.comparison);
            return;
        }
        if (lhs.kind == DirectOperandKind::ReciprocalShiftX && rhs.kind == DirectOperandKind::YVariable)
        {
            assignPlan(lhs, invertedComparison(root.comparison));
        }
    }

    void initializeTangentAxisCurvePlan()
    {
        if (!formula.rootComparisonBytecode || !xSlot || !ySlot)
        {
            return;
        }

        const auto &root = *formula.rootComparisonBytecode;
        const auto lhs = operandPlanFor(root.lhs);
        const auto rhs = operandPlanFor(root.rhs);
        const auto isTangentAxis = [](const OperandPlan &plan, const size_t axisSlot)
        {
            return plan.kind == DirectOperandKind::UnaryVariable
                && plan.variableSlot == axisSlot
                && plan.unaryCount == 1
                && plan.unaryOps[0] == FormulaOp::Tan
                && std::isfinite(plan.constant);
        };
        const auto assignPlan = [this](const OperandPlan &tangent,
                                       const bool tangentAxisIsX,
                                       const FormulaOp tangentComparison)
        {
            if (supportedComparisonForCurve(tangentComparison))
            {
                tangentAxisCurvePlan = TangentAxisCurvePlan{
                    .tangentAxisIsX = tangentAxisIsX,
                    .tangentScale = tangent.constant,
                    .tangentComparison = tangentComparison
                };
            }
        };

        if (isTangentAxis(lhs, *xSlot) && rhs.kind == DirectOperandKind::YVariable)
        {
            assignPlan(lhs, true, root.comparison);
            return;
        }
        if (lhs.kind == DirectOperandKind::YVariable && isTangentAxis(rhs, *xSlot))
        {
            assignPlan(rhs, true, invertedComparison(root.comparison));
            return;
        }
        if (isTangentAxis(lhs, *ySlot) && rhs.kind == DirectOperandKind::XVariable)
        {
            assignPlan(lhs, false, root.comparison);
            return;
        }
        if (lhs.kind == DirectOperandKind::XVariable && isTangentAxis(rhs, *ySlot))
        {
            assignPlan(rhs, false, invertedComparison(root.comparison));
        }
    }

    void initializeMonotoneUnaryCurvePlan()
    {
        if (!formula.rootComparisonBytecode || !xSlot || !ySlot)
        {
            return;
        }

        const auto &root = *formula.rootComparisonBytecode;
        const auto lhs = operandPlanFor(root.lhs);
        const auto rhs = operandPlanFor(root.rhs);
        const auto isUnaryX = [this](const OperandPlan &plan)
        {
            return plan.kind == DirectOperandKind::UnaryVariable
                && plan.variableSlot == *xSlot
                && plan.constant > 0.0
                && plan.unaryCount == 1
                && supportedMonotoneUnaryCurveOp(plan.unaryOps[0]);
        };
        const auto assignPlan = [this](const FormulaOp unaryOp, const FormulaOp yComparison)
        {
            if (supportedComparisonForCurve(yComparison))
            {
                monotoneUnaryCurvePlan = MonotoneUnaryCurvePlan{
                    .unaryOp = unaryOp,
                    .yComparison = yComparison
                };
            }
        };

        if (lhs.kind == DirectOperandKind::YVariable && isUnaryX(rhs))
        {
            assignPlan(rhs.unaryOps[0], root.comparison);
            return;
        }
        if (isUnaryX(lhs) && rhs.kind == DirectOperandKind::YVariable)
        {
            assignPlan(lhs.unaryOps[0], invertedComparison(root.comparison));
            return;
        }
        if (lhs.kind == DirectOperandKind::UnaryXMinusY
            && lhs.unaryCount == 1
            && supportedMonotoneUnaryCurveOp(lhs.unaryOps[0])
            && rhs.kind == DirectOperandKind::Constant
            && rhs.constant == 0.0)
        {
            if (const auto yComparison = yComparisonForFxMinusY(root.comparison))
            {
                assignPlan(lhs.unaryOps[0], *yComparison);
            }
            return;
        }
        if (lhs.kind == DirectOperandKind::YMinusUnaryX
            && lhs.unaryCount == 1
            && supportedMonotoneUnaryCurveOp(lhs.unaryOps[0])
            && rhs.kind == DirectOperandKind::Constant
            && rhs.constant == 0.0)
        {
            if (supportedComparisonForCurve(root.comparison))
            {
                assignPlan(lhs.unaryOps[0], root.comparison);
            }
        }
    }

    [[nodiscard]] bool axisCacheAddressFor(const TileKey &key, AxisCacheAddress &address) const
    {
        if (!rootAxisOnlyOperandCache)
        {
            return false;
        }

        const auto shift = previewKey.level - key.level;
        if (shift < 0 || shift > RootAxisOnlyOperandCache::MaxCachedAxisShift)
        {
            return false;
        }

        const auto keyIndex = rootAxisOnlyOperandCache->axisIsY ? key.y : key.x;
        const auto shiftIndex = static_cast<size_t>(shift);
        if (rootAxisOnlyOperandCache->addressValid[shiftIndex] == uint8_t{0})
        {
            return false;
        }

        const auto scale = rootAxisOnlyOperandCache->scaleByShift[shiftIndex];
        const auto baseIndex = rootAxisOnlyOperandCache->baseIndexByShift[shiftIndex];
        const auto relativeIndex = keyIndex - baseIndex;
        if (relativeIndex < 0 || relativeIndex >= scale)
        {
            return false;
        }

        address = AxisCacheAddress{
            .shiftIndex = shiftIndex,
            .scale = scale,
            .baseIndex = baseIndex,
            .relativeIndex = static_cast<size_t>(relativeIndex)
        };
        return true;
    }

    [[nodiscard]] IntervalLevelCache *intervalLevelCacheFor(const TileKey &key, size_t &relativeIndex)
    {
        if (!rootAxisOnlyOperandCache)
        {
            return nullptr;
        }

        auto address = AxisCacheAddress{};
        if (!axisCacheAddressFor(key, address))
        {
            return nullptr;
        }

        relativeIndex = address.relativeIndex;
        auto &levelCache = rootAxisOnlyOperandCache->intervalLevels[address.shiftIndex];
        if (!levelCache)
        {
            levelCache = IntervalLevelCache{
                .level = key.level,
                .baseIndex = address.baseIndex,
                .values = std::vector<Interval>(static_cast<size_t>(address.scale)),
                .valid = std::vector<uint8_t>(static_cast<size_t>(address.scale), uint8_t{0})
            };
        }
        return &*levelCache;
    }

    [[nodiscard]] PointLevelCache *pointLevelCacheFor(const TileKey &key, size_t &relativeIndex)
    {
        if (!rootAxisOnlyOperandCache)
        {
            return nullptr;
        }

        auto address = AxisCacheAddress{};
        if (!axisCacheAddressFor(key, address))
        {
            return nullptr;
        }

        relativeIndex = address.relativeIndex;
        auto &levelCache = rootAxisOnlyOperandCache->pointLevels[address.shiftIndex];
        if (!levelCache)
        {
            levelCache = PointLevelCache{
                .level = key.level,
                .baseIndex = address.baseIndex,
                .centers = std::vector<double>(static_cast<size_t>(address.scale), 0.0),
                .centerValid = std::vector<uint8_t>(static_cast<size_t>(address.scale), uint8_t{0}),
                .boundaries = std::vector<double>(static_cast<size_t>(address.scale) + 1, 0.0),
                .boundaryValid = std::vector<uint8_t>(static_cast<size_t>(address.scale) + 1, uint8_t{0})
            };
        }
        return &*levelCache;
    }

    [[nodiscard]] Interval evaluateCachedAxisOnlyOperand(const TileKey &key,
                                                         const Interval &xRange,
                                                         const Interval &yRange)
    {
        auto index = size_t{0};
        auto *levelCache = intervalLevelCacheFor(key, index);
        if (!levelCache)
        {
            return evaluateOperandInterval(rootAxisOnlyOperandCache->cachedOperand, xRange, yRange);
        }

        if (levelCache->valid[index] == uint8_t{0})
        {
            levelCache->values[index] =
                evaluateOperandInterval(rootAxisOnlyOperandCache->cachedOperand, xRange, yRange);
            levelCache->valid[index] = uint8_t{1};
        }
        return levelCache->values[index];
    }

    [[nodiscard]] Interval evaluateRootComparisonWithCache(const TileKey &key,
                                                           const Interval &xRange,
                                                           const Interval &yRange)
    {
        const auto cached = evaluateCachedAxisOnlyOperand(key, xRange, yRange);
        if (sinProductAxisComparisonPlan)
        {
            const auto sineRangeValue = sineRange(scaleRange(
                productRange(xRange, yRange),
                sinProductAxisComparisonPlan->sineScale));
            return compareIntervalOperands(
                sineRangeValue,
                cached,
                sinProductAxisComparisonPlan->sineComparison);
        }

        const auto other = evaluateOperandInterval(rootAxisOnlyOperandCache->otherOperand, xRange, yRange);
        return rootAxisOnlyOperandCache->cachedOperandIsLhs
            ? compareIntervalOperands(cached, other, formula.rootComparisonBytecode->comparison)
            : compareIntervalOperands(other, cached, formula.rootComparisonBytecode->comparison);
    }

    [[nodiscard]] Interval evaluateRootComparisonDirect(const Interval &xRange, const Interval &yRange)
    {
        const auto lhs = evaluateOperandInterval(rootComparisonDirectPlan->lhs, xRange, yRange);
        const auto rhs = evaluateOperandInterval(rootComparisonDirectPlan->rhs, xRange, yRange);
        return compareIntervalOperands(lhs, rhs, rootComparisonDirectPlan->comparison);
    }

    [[nodiscard]] double evaluateCachedAxisOnlyPointOperand(const TileKey &key,
                                                            const PointAxisKind kind,
                                                            const double x,
                                                            const double y)
    {
        auto relativeIndex = size_t{0};
        auto *levelCache = pointLevelCacheFor(key, relativeIndex);
        if (!levelCache)
        {
            return evaluateOperandDouble(rootAxisOnlyOperandCache->cachedOperand, x, y);
        }

        if (kind == PointAxisKind::Center)
        {
            if (levelCache->centerValid[relativeIndex] == uint8_t{0})
            {
                levelCache->centers[relativeIndex] =
                    evaluateOperandDouble(rootAxisOnlyOperandCache->cachedOperand, x, y);
                levelCache->centerValid[relativeIndex] = uint8_t{1};
            }
            return levelCache->centers[relativeIndex];
        }

        const auto boundaryIndex = relativeIndex + (kind == PointAxisKind::UpperBoundary ? 1 : 0);
        if (levelCache->boundaryValid[boundaryIndex] == uint8_t{0})
        {
            levelCache->boundaries[boundaryIndex] =
                evaluateOperandDouble(rootAxisOnlyOperandCache->cachedOperand, x, y);
            levelCache->boundaryValid[boundaryIndex] = uint8_t{1};
        }
        return levelCache->boundaries[boundaryIndex];
    }

    [[nodiscard]] bool evaluateRootComparisonPointWithCache(const TileKey &key,
                                                            const PointAxisKind xKind,
                                                            const PointAxisKind yKind,
                                                            const double x,
                                                            const double y)
    {
        const auto cached = rootAxisOnlyOperandCache->axisIsY
            ? evaluateCachedAxisOnlyPointOperand(key, yKind, x, y)
            : evaluateCachedAxisOnlyPointOperand(key, xKind, x, y);
        if (sinProductAxisComparisonPlan)
        {
            const auto sineValue = std::sin(sinProductAxisComparisonPlan->sineScale * x * y);
            return compareDoubleOperands(
                sineValue,
                cached,
                sinProductAxisComparisonPlan->sineComparison);
        }

        const auto other = evaluateOperandDouble(rootAxisOnlyOperandCache->otherOperand, x, y);
        return rootAxisOnlyOperandCache->cachedOperandIsLhs
            ? compareDoubleOperands(cached, other, formula.rootComparisonBytecode->comparison)
            : compareDoubleOperands(other, cached, formula.rootComparisonBytecode->comparison);
    }

    [[nodiscard]] bool evaluateRootComparisonPointDirect(const double x, const double y)
    {
        const auto lhs = evaluateOperandDouble(rootComparisonDirectPlan->lhs, x, y);
        const auto rhs = evaluateOperandDouble(rootComparisonDirectPlan->rhs, x, y);
        return compareDoubleOperands(lhs, rhs, rootComparisonDirectPlan->comparison);
    }

    [[nodiscard]] EvaluatedBox evaluate(const TileKey &key, const Rect &bounds)
    {
        const auto xRange = Interval{bounds.xMin, bounds.xMax};
        const auto yRange = Interval{bounds.yMin, bounds.yMax};

        ++intervalEvaluations;
        try
        {
            auto interval = Interval{};
            if (rootAxisOnlyOperandCache)
            {
                interval = evaluateRootComparisonWithCache(key, xRange, yRange);
            }
            else if (rootComparisonDirectPlan)
            {
                interval = evaluateRootComparisonDirect(xRange, yRange);
            }
            else
            {
                setIntervalVariables(xRange, yRange);
                interval = formula.evaluateInterval(intervalVariables, evaluationStack);
            }
            return evaluatedBoxFor(interval);
        }
        catch (...)
        {
            return {.classification = TileClassification::Mixed, .interval = INTERVAL_UNDEFINED};
        }
    }

    [[nodiscard]] bool containsTruePoint(const TileKey &key,
                                         const PointAxisKind xKind,
                                         const PointAxisKind yKind,
                                         const double x,
                                         const double y)
    {
        try
        {
            ++pointEvaluations;
            if (rootAxisOnlyOperandCache)
            {
                return evaluateRootComparisonPointWithCache(key, xKind, yKind, x, y);
            }
            if (rootComparisonDirectPlan)
            {
                return evaluateRootComparisonPointDirect(x, y);
            }

            setDoubleVariables(x, y);
            const auto value = formula.evaluateDouble(doubleVariables, doubleEvaluationStack);
            return std::isfinite(value) && value != 0.0;
        }
        catch (...)
        {
            return false;
        }
    }

    [[nodiscard]] bool containsTrueCenterPoint(const TileKey &key, const Rect &bounds)
    {
        return containsTruePoint(
            key,
            PointAxisKind::Center,
            PointAxisKind::Center,
            (bounds.xMin + bounds.xMax) * 0.5,
            (bounds.yMin + bounds.yMax) * 0.5);
    }

    [[nodiscard]] bool containsTrueCornerPoint(const TileKey &key, const Rect &bounds)
    {
        return containsTruePoint(
                key,
                PointAxisKind::LowerBoundary,
                PointAxisKind::LowerBoundary,
                bounds.xMin,
                bounds.yMin)
            || containsTruePoint(
                key,
                PointAxisKind::UpperBoundary,
                PointAxisKind::LowerBoundary,
                bounds.xMax,
                bounds.yMin)
            || containsTruePoint(
                key,
                PointAxisKind::LowerBoundary,
                PointAxisKind::UpperBoundary,
                bounds.xMin,
                bounds.yMax)
            || containsTruePoint(
                key,
                PointAxisKind::UpperBoundary,
                PointAxisKind::UpperBoundary,
                bounds.xMax,
                bounds.yMax);
    }

    void recordProofNode(const TileKey &key,
                         const EvaluatedBox &evaluated,
                         const TileExistenceState existence)
    {
        if (recordOnlyRootProof && key != previewKey)
        {
            return;
        }

        region.proofTree.nodes.push_back(TileProofNode{
            .key = key,
            .classification = evaluated.classification,
            .existence = existence,
            .interval = evaluated.interval
        });
    }

    void recordPointExistenceNode(const TileKey &key)
    {
        if (recordOnlyRootProof && key != previewKey)
        {
            return;
        }

        region.proofTree.nodes.push_back(TileProofNode{
            .key = key,
            .classification = TileClassification::Mixed,
            .existence = TileExistenceState::Exists
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
        if (value == PixelUnknown)
        {
            region.certainty = TextureCertainty::BestEstimate;
            unknownPixels += static_cast<size_t>(width) * static_cast<size_t>(width);
        }
        for (auto y = 0; y < width; ++y)
        {
            std::fill_n(
                region.pixels.begin() + static_cast<std::ptrdiff_t>(pixelIndex(startX, startY + y)),
                static_cast<size_t>(width),
                value);
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

    [[nodiscard]] TileExistenceState refineSubpixel(const TileKey &key,
                                                     const Rect &bounds)
    {
        if (cancelled() || nodeBudgetExhausted())
        {
            return TileExistenceState::Unknown;
        }

        ++visitedNodes;
        const auto evaluated = evaluate(key, bounds);
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
        if (containsTrueCenterPoint(key, bounds))
        {
            recordPointExistenceNode(key);
            return TileExistenceState::Exists;
        }
        if (evaluated.interval.undefined())
        {
            recordProofNode(key, evaluated, TileExistenceState::Unknown);
            return TileExistenceState::Unknown;
        }

        if (key.level <= subpixelLevel)
        {
            recordProofNode(key, evaluated, TileExistenceState::Unknown);
            return TileExistenceState::Unknown;
        }

        auto allEmpty = true;
        const auto xMid = (bounds.xMin + bounds.xMax) * 0.5;
        const auto yMid = (bounds.yMin + bounds.yMax) * 0.5;
        const auto visitChild = [&](const TileKey &child, const Rect &childBounds)
        {
            const auto childExistence = refineSubpixel(child, childBounds);
            if (childExistence == TileExistenceState::Exists)
            {
                return true;
            }
            if (childExistence != TileExistenceState::Empty)
            {
                allEmpty = false;
            }
            return false;
        };

        if (visitChild(TileKey{key.x * 2, key.y * 2, key.level - 1}, Rect{bounds.xMin, xMid, bounds.yMin, yMid})
            || visitChild(TileKey{key.x * 2 + 1, key.y * 2, key.level - 1}, Rect{xMid, bounds.xMax, bounds.yMin, yMid})
            || visitChild(TileKey{key.x * 2, key.y * 2 + 1, key.level - 1}, Rect{bounds.xMin, xMid, yMid, bounds.yMax})
            || visitChild(TileKey{key.x * 2 + 1, key.y * 2 + 1, key.level - 1}, Rect{xMid, bounds.xMax, yMid, bounds.yMax}))
        {
            return TileExistenceState::Exists;
        }

        const auto existence = allEmpty ? TileExistenceState::Empty : TileExistenceState::Unknown;
        recordProofNode(key, evaluated, existence);
        return existence;
    }

    [[nodiscard]] TileExistenceState refinePixel(const TileKey &key,
                                                 const Rect &bounds,
                                                 const EvaluatedBox &evaluated)
    {
        if (containsTrueCornerPoint(key, bounds))
        {
            setPixelFor(key, TileExistenceState::Exists);
            recordProofNode(key, evaluated, TileExistenceState::Exists);
            return TileExistenceState::Exists;
        }
        if (evaluated.interval.undefined())
        {
            setPixelFor(key, TileExistenceState::Unknown);
            recordProofNode(key, evaluated, TileExistenceState::Unknown);
            return TileExistenceState::Unknown;
        }
        if (key.level <= subpixelLevel)
        {
            setPixelFor(key, TileExistenceState::Unknown);
            recordProofNode(key, evaluated, TileExistenceState::Unknown);
            return TileExistenceState::Unknown;
        }

        auto allEmpty = true;
        const auto xMid = (bounds.xMin + bounds.xMax) * 0.5;
        const auto yMid = (bounds.yMin + bounds.yMax) * 0.5;
        const auto visitChild = [&](const TileKey &child, const Rect &childBounds)
        {
            const auto childExistence = refineSubpixel(child, childBounds);
            if (childExistence == TileExistenceState::Exists)
            {
                return true;
            }
            if (childExistence != TileExistenceState::Empty)
            {
                allEmpty = false;
            }
            return false;
        };

        if (visitChild(TileKey{key.x * 2, key.y * 2, key.level - 1}, Rect{bounds.xMin, xMid, bounds.yMin, yMid})
            || visitChild(TileKey{key.x * 2 + 1, key.y * 2, key.level - 1}, Rect{xMid, bounds.xMax, bounds.yMin, yMid})
            || visitChild(TileKey{key.x * 2, key.y * 2 + 1, key.level - 1}, Rect{bounds.xMin, xMid, yMid, bounds.yMax})
            || visitChild(TileKey{key.x * 2 + 1, key.y * 2 + 1, key.level - 1}, Rect{xMid, bounds.xMax, yMid, bounds.yMax}))
        {
            setPixelFor(key, TileExistenceState::Exists);
            recordProofNode(key, evaluated, TileExistenceState::Exists);
            return TileExistenceState::Exists;
        }

        const auto existence = allEmpty ? TileExistenceState::Empty : TileExistenceState::Unknown;
        setPixelFor(key, existence);
        recordProofNode(key, evaluated, existence);
        return existence;
    }

    [[nodiscard]] TileExistenceState refineAbovePixel(const TileKey &key, const Rect &bounds)
    {
        if (cancelled() || nodeBudgetExhausted())
        {
            return TileExistenceState::Unknown;
        }

        ++visitedNodes;
        if (key.level == pixelLevel && containsTrueCenterPoint(key, bounds))
        {
            setPixelFor(key, TileExistenceState::Exists);
            recordPointExistenceNode(key);
            return TileExistenceState::Exists;
        }

        const auto evaluated = evaluate(key, bounds);
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
        if (evaluated.interval.undefined())
        {
            fillPixelsFor(key, PixelUnknown);
            recordProofNode(key, evaluated, TileExistenceState::Unknown);
            return TileExistenceState::Unknown;
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
        const auto xMid = (bounds.xMin + bounds.xMax) * 0.5;
        const auto yMid = (bounds.yMin + bounds.yMax) * 0.5;
        const auto visitChild = [&](const TileKey &child, const Rect &childBounds)
        {
            const auto childExistence = refineAbovePixel(child, childBounds);
            anyExists = anyExists || childExistence == TileExistenceState::Exists;
            allEmpty = allEmpty && childExistence == TileExistenceState::Empty;
        };
        visitChild(TileKey{key.x * 2, key.y * 2, key.level - 1}, Rect{bounds.xMin, xMid, bounds.yMin, yMid});
        visitChild(TileKey{key.x * 2 + 1, key.y * 2, key.level - 1}, Rect{xMid, bounds.xMax, bounds.yMin, yMid});
        visitChild(TileKey{key.x * 2, key.y * 2 + 1, key.level - 1}, Rect{bounds.xMin, xMid, yMid, bounds.yMax});
        visitChild(TileKey{key.x * 2 + 1, key.y * 2 + 1, key.level - 1}, Rect{xMid, bounds.xMax, yMid, bounds.yMax});

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
    int parallelSplitDepth{0};
    int pixelLevel{0};
    int subpixelLevel{0};
    Rect rootBounds{};
    bool hasCancellationCallback{false};
    bool hasNodeBudget{false};
    size_t maxVisitedNodes{0};
    bool recordOnlyRootProof{true};
    std::optional<size_t> xSlot{};
    std::optional<size_t> ySlot{};
    std::vector<Interval> intervalVariables;
    std::vector<Interval> evaluationStack;
    std::optional<RootAxisOnlyOperandCache> rootAxisOnlyOperandCache{};
    std::optional<RootComparisonDirectPlan> rootComparisonDirectPlan{};
    std::optional<MonotoneUnaryCurvePlan> monotoneUnaryCurvePlan{};
    std::optional<ReciprocalCurvePlan> reciprocalCurvePlan{};
    std::optional<TangentAxisCurvePlan> tangentAxisCurvePlan{};
    std::optional<SinProductAxisComparisonPlan> sinProductAxisComparisonPlan{};
    std::optional<SumSquaresDiskPlan> sumSquaresDiskPlan{};
    std::optional<SquareDifferenceBandPlan> squareDifferenceBandPlan{};
    std::vector<double> doubleVariables;
    std::vector<double> doubleEvaluationStack;
    RegionOutput region{};
    size_t visitedNodes{0};
    size_t unknownPixels{0};
    size_t intervalEvaluations{0};
    size_t pointEvaluations{0};
};

InequalityTileRefinementResult refineInequalityTileInternal(
    const CompiledFormula &formula,
    const TileKey &previewKey,
    const InequalityTileRefinementOptions &options,
    const int parallelSplitDepth)
{
    const auto pixelDepth = exactLog2(options.pixelsPerAxis);
    if (!pixelDepth)
    {
        return {
            .ok = false,
            .message = "Inequality tile refinement requires a power-of-two texture size"
        };
    }

    auto run = RefinementRun{formula, previewKey, options, *pixelDepth, parallelSplitDepth};
    return run.run();
}
}

InequalityTileRefinementResult refineInequalityTile(
    const CompiledFormula &formula,
    const TileKey &previewKey,
    const InequalityTileRefinementOptions &options)
{
    return refineInequalityTileInternal(
        formula,
        previewKey,
        options,
        defaultParallelSplitDepth(options.pixelsPerAxis));
}
}
