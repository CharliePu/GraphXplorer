#include "catch.hpp"

#include <array>

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
