#ifndef AFFINEINEQUALITY_H
#define AFFINEINEQUALITY_H

#include "../Formula/FormulaCompiler.h"

namespace gx
{
[[nodiscard]] inline Interval affineValueRange(const AffineInequality &affine, const Rect &bounds)
{
    return {
        affine.constant
            + (affine.xCoefficient >= 0.0
                ? affine.xCoefficient * bounds.xMin
                : affine.xCoefficient * bounds.xMax)
            + (affine.yCoefficient >= 0.0
                ? affine.yCoefficient * bounds.yMin
                : affine.yCoefficient * bounds.yMax),
        affine.constant
            + (affine.xCoefficient >= 0.0
                ? affine.xCoefficient * bounds.xMax
                : affine.xCoefficient * bounds.xMin)
            + (affine.yCoefficient >= 0.0
                ? affine.yCoefficient * bounds.yMax
                : affine.yCoefficient * bounds.yMin)
    };
}

[[nodiscard]] inline Interval truthRangeForAffineValue(const Interval &valueRange, const FormulaOp comparison)
{
    switch (comparison)
    {
    case FormulaOp::Less:
        if (valueRange.upper < 0.0) return Interval{1.0};
        if (valueRange.lower >= 0.0) return Interval{0.0};
        break;
    case FormulaOp::LessEqual:
        if (valueRange.upper <= 0.0) return Interval{1.0};
        if (valueRange.lower > 0.0) return Interval{0.0};
        break;
    case FormulaOp::Greater:
        if (valueRange.lower > 0.0) return Interval{1.0};
        if (valueRange.upper <= 0.0) return Interval{0.0};
        break;
    case FormulaOp::GreaterEqual:
        if (valueRange.lower >= 0.0) return Interval{1.0};
        if (valueRange.upper < 0.0) return Interval{0.0};
        break;
    case FormulaOp::Equal:
        if (valueRange.lower == 0.0 && valueRange.upper == 0.0) return Interval{1.0};
        if (valueRange.upper < 0.0 || valueRange.lower > 0.0) return Interval{0.0};
        break;
    case FormulaOp::NotEqual:
        if (valueRange.upper < 0.0 || valueRange.lower > 0.0) return Interval{1.0};
        if (valueRange.lower == 0.0 && valueRange.upper == 0.0) return Interval{0.0};
        break;
    default:
        break;
    }
    return Interval{0.0, 1.0};
}

[[nodiscard]] inline bool affineCanBeTrue(const double lower, const double upper, const FormulaOp comparison)
{
    switch (comparison)
    {
    case FormulaOp::Less:
        return lower < 0.0;
    case FormulaOp::LessEqual:
        return lower <= 0.0;
    case FormulaOp::Greater:
        return upper > 0.0;
    case FormulaOp::GreaterEqual:
        return upper >= 0.0;
    case FormulaOp::Equal:
        return lower <= 0.0 && upper >= 0.0;
    case FormulaOp::NotEqual:
        return lower != 0.0 || upper != 0.0;
    default:
        return false;
    }
}

[[nodiscard]] inline bool affineCanBeTrue(const Interval &valueRange, const FormulaOp comparison)
{
    return affineCanBeTrue(valueRange.lower, valueRange.upper, comparison);
}

[[nodiscard]] inline TileClassification classificationForTruthRange(const Interval &truthRange)
{
    if (truthRange.allTrue())
    {
        return TileClassification::UniformTrue;
    }
    if (truthRange.allFalse())
    {
        return TileClassification::UniformFalse;
    }
    return TileClassification::Mixed;
}
}

#endif // AFFINEINEQUALITY_H
