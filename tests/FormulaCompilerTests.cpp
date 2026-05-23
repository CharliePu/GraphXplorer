#include "catch.hpp"

#include <array>
#include <cmath>
#include <vector>

#include "../src/Compute/ComputeBackend.h"
#include "../src/Formula/Formula.h"
#include "../src/Formula/FormulaCompiler.h"

TEST_CASE("FormulaCompiler emits stable bytecode and preserves double evaluation", "[FormulaCompiler]")
{
    const auto compiled = gx::FormulaCompiler{}.compile("x^2+y^2<4^2");

    REQUIRE(compiled.diagnostics.ok);
    CHECK(compiled.handle.valid());
    CHECK_FALSE(compiled.evaluationIr.empty());
    CHECK(compiled.evaluateDouble({{"x", 1.0}, {"y", 1.0}}) == 1.0);
    CHECK(compiled.evaluateDouble({{"x", 5.0}, {"y", 5.0}}) == 0.0);
}

TEST_CASE("FormulaCompiler preserves powered comparisons", "[FormulaCompiler]")
{
    const auto compiled = gx::FormulaCompiler{}.compile("(x-y)^2<0.0001");

    REQUIRE(compiled.diagnostics.ok);
    CHECK_FALSE(compiled.affineInequality.has_value());
    CHECK(compiled.evaluateDouble({{"x", 0.0}, {"y", 0.0}}) == 1.0);
    CHECK(compiled.evaluateDouble({{"x", 0.0}, {"y", 1.0}}) == 0.0);

    const auto interval = compiled.evaluateInterval({
        {"x", Interval{-1.0, 1.0}},
        {"y", Interval{-1.0, 1.0}}
    });
    CHECK(interval.lower == 0.0);
    CHECK(interval.upper == 1.0);
}

TEST_CASE("FormulaCompiler canonicalizes commutative semantic expressions", "[FormulaCompiler]")
{
    const auto lhs = gx::FormulaCompiler{}.compile("x+y>0");
    const auto rhs = gx::FormulaCompiler{}.compile("y+x>0");

    REQUIRE(lhs.diagnostics.ok);
    REQUIRE(rhs.diagnostics.ok);
    CHECK(lhs.handle.sourceHash.value != rhs.handle.sourceHash.value);
    CHECK(lhs.handle.semanticsHash == rhs.handle.semanticsHash);
}

TEST_CASE("Formula wrapper delegates evaluation to compiled formula", "[FormulaCompiler][Formula]")
{
    Formula formula{"x<=y"};

    CHECK(formula.getCompiledFormula().handle.valid());
    CHECK(formula.evaluate({{"x", 1.0}, {"y", 2.0}}) == 1.0);
    CHECK(formula.evaluate({{"x", 3.0}, {"y", 2.0}}) == 0.0);
}

TEST_CASE("FormulaCompiler detects affine inequality residuals", "[FormulaCompiler][Compute]")
{
    const auto affine = gx::FormulaCompiler{}.compile("2*x-y/4+3<=5");
    REQUIRE(affine.diagnostics.ok);
    REQUIRE(affine.affineInequality.has_value());
    CHECK(affine.affineInequality->xCoefficient == 2.0);
    CHECK(affine.affineInequality->yCoefficient == -0.25);
    CHECK(affine.affineInequality->constant == -2.0);
    CHECK(affine.affineInequality->comparison == gx::FormulaOp::LessEqual);

    const auto nonlinear = gx::FormulaCompiler{}.compile("sin(x)<y");
    REQUIRE(nonlinear.diagnostics.ok);
    CHECK_FALSE(nonlinear.affineInequality.has_value());
}

TEST_CASE("FormulaCompiler detects tangent pole inequalities", "[FormulaCompiler][Compute]")
{
    const auto direct = gx::FormulaCompiler{}.compile("tan(x)>y");
    REQUIRE(direct.diagnostics.ok);
    REQUIRE(direct.tangentPoleInequality.has_value());
    CHECK(direct.tangentPoleInequality->argumentXCoefficient == 1.0);
    CHECK(direct.tangentPoleInequality->argumentYCoefficient == 0.0);
    CHECK(direct.tangentPoleInequality->argumentConstant == 0.0);
    CHECK(direct.tangentPoleInequality->comparison == gx::FormulaOp::Greater);

    const auto flipped = gx::FormulaCompiler{}.compile("y<=tan(2*x)");
    REQUIRE(flipped.diagnostics.ok);
    REQUIRE(flipped.tangentPoleInequality.has_value());
    CHECK(flipped.tangentPoleInequality->argumentXCoefficient == 2.0);
    CHECK(flipped.tangentPoleInequality->comparison == gx::FormulaOp::GreaterEqual);

    const auto equality = gx::FormulaCompiler{}.compile("tan(x)=y");
    REQUIRE(equality.diagnostics.ok);
    CHECK_FALSE(equality.tangentPoleInequality.has_value());
}

TEST_CASE("FormulaCompiler records root comparison bytecode operands", "[FormulaCompiler][Compute]")
{
    const auto compiled = gx::FormulaCompiler{}.compile("sin(x*y)<sin(sin(y))");
    REQUIRE(compiled.diagnostics.ok);
    REQUIRE(compiled.rootComparisonBytecode.has_value());

    const auto xSlot = compiled.variableSlot("x");
    const auto ySlot = compiled.variableSlot("y");
    REQUIRE(xSlot.has_value());
    REQUIRE(ySlot.has_value());

    const auto xMask = uint64_t{1} << *xSlot;
    const auto yMask = uint64_t{1} << *ySlot;
    const auto &root = *compiled.rootComparisonBytecode;
    CHECK(root.comparison == gx::FormulaOp::Less);
    CHECK(root.lhs.variableMask == (xMask | yMask));
    CHECK(root.rhs.variableMask == yMask);

    std::vector<double> variables(compiled.variableNames.size(), 0.0);
    variables[*xSlot] = 2.0;
    variables[*ySlot] = 0.5;
    std::vector<double> stack;
    CHECK(compiled.evaluateDoubleBytecode(root.lhs, variables, stack) == Catch::Approx(std::sin(1.0)));
    CHECK(compiled.evaluateDoubleBytecode(root.rhs, variables, stack) == Catch::Approx(std::sin(std::sin(0.5))));
}

TEST_CASE("FormulaCompiler simplifies identity comparisons before interval classification", "[FormulaCompiler][Compute]")
{
    gx::FormulaCompiler compiler;

    const auto equal = compiler.compile("x=x");
    REQUIRE(equal.diagnostics.ok);
    CHECK(equal.canonicalExpression == "1");
    CHECK(equal.evaluateDouble({{"x", -7.0}}) == 1.0);

    const auto less = compiler.compile("x<x");
    REQUIRE(less.diagnostics.ok);
    CHECK(less.canonicalExpression == "0");
    CHECK(less.evaluateDouble({{"x", -7.0}}) == 0.0);

    const auto notEqual = compiler.compile("x!=x");
    REQUIRE(notEqual.diagnostics.ok);
    CHECK(notEqual.canonicalExpression == "0");

    const auto residualIdentity = compiler.compile("(x+y)-(y+x)=0");
    REQUIRE(residualIdentity.diagnostics.ok);
    CHECK(residualIdentity.canonicalExpression == "1");

    gx::CpuComputeBackend backend;
    const std::array keys{gx::TileKey{0, 0, 0}};
    const std::array xMin{-1.0};
    const std::array xMax{1.0};
    const std::array yMin{-1.0};
    const std::array yMax{1.0};
    std::array<gx::TileClassificationResult, 1> out{};

    const auto result = backend.classifyIntervals(
        gx::IntervalBatchView{&equal, keys, xMin, xMax, yMin, yMax, {}},
        out);

    REQUIRE(result.ok);
    REQUIRE(result.completed == 1);
    CHECK(out.front().classification == gx::TileClassification::UniformTrue);
    CHECK(out.front().interval.allTrue());
}
