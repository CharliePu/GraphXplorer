#include "catch.hpp"

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
