#include <catch2/catch_test_macros.hpp>

#include "expr/Relation.h"

using namespace gxr;

namespace
{
Relation must(const std::string &src)
{
    std::string err;
    auto r = Relation::parse(src, err);
    REQUIRE(r.has_value());
    return std::move(*r);
}

Interval iv(double lo, double hi) { return Interval{lo, hi}; }
}

TEST_CASE("parser rejects garbage and accepts relations", "[expr]")
{
    std::string err;
    REQUIRE_FALSE(Relation::parse("x +", err).has_value());
    REQUIRE_FALSE(Relation::parse("foo(x)", err).has_value());
    REQUIRE(Relation::parse("y > sin(2^x)", err).has_value());
    REQUIRE(Relation::parse("x^2 + y^2 < 1", err).has_value());
    REQUIRE(Relation::parse("y = x^2", err).has_value());
    REQUIRE(Relation::parse("x>0 && y>0", err).has_value());
}

TEST_CASE("half-plane y > x classifies boxes soundly", "[expr]")
{
    Relation r = must("y > x");
    EvalScratch s;
    // box fully above the line y=x  (y in [2,3], x in [0,1]) -> all true
    REQUIRE(r.classifyBox(iv(0.0, 1.0), iv(2.0, 3.0), s) == Sign::AllTrue);
    // box fully below -> all false
    REQUIRE(r.classifyBox(iv(2.0, 3.0), iv(0.0, 1.0), s) == Sign::AllFalse);
    // straddling the diagonal -> uncertain
    REQUIRE(r.classifyBox(iv(0.0, 2.0), iv(0.0, 2.0), s) == Sign::Uncertain);
}

TEST_CASE("centered form certifies disk interior/exterior (dependency win)", "[expr]")
{
    Relation r = must("x^2 + y^2 < 1");
    EvalScratch s;
    // tiny box near origin -> certainly inside
    REQUIRE(r.classifyBox(iv(-0.1, 0.1), iv(-0.1, 0.1), s) == Sign::AllTrue);
    // box far outside -> certainly outside
    REQUIRE(r.classifyBox(iv(3.0, 4.0), iv(3.0, 4.0), s) == Sign::AllFalse);
    // box crossing the unit circle -> uncertain
    REQUIRE(r.classifyBox(iv(0.6, 0.8), iv(0.6, 0.8), s) == Sign::Uncertain);
}

TEST_CASE("explicit-1D structure is detected and normalized", "[expr]")
{
    {
        Relation r = must("y < sin(x)");
        REQUIRE(r.explicit1D());
        REQUIRE(r.explicitIsY());
        REQUIRE(r.explicitOp() == CmpOp::Less);
    }
    {
        // g(x) on the left: sin(x) > y  <=>  y < sin(x)
        Relation r = must("sin(x) > y");
        REQUIRE(r.explicit1D());
        REQUIRE(r.explicitIsY());
        REQUIRE(r.explicitOp() == CmpOp::Less);
    }
    {
        // explicit in x: x > sin(y)
        Relation r = must("x > sin(y)");
        REQUIRE(r.explicit1D());
        REQUIRE_FALSE(r.explicitIsY());
        REQUIRE(r.explicitOp() == CmpOp::Greater);
    }
    {
        // not explicit: y appears on both sides
        Relation r = must("y + x > sin(y)");
        REQUIRE_FALSE(r.explicit1D());
    }
}

TEST_CASE("equality is flagged and never classifies a fat box as all-true", "[expr]")
{
    Relation r = must("y = x^2");
    EvalScratch s;
    REQUIRE(r.isEquality());
    // a box straddling the parabola is uncertain, never AllTrue
    const Sign onCurve = r.classifyBox(iv(-0.1, 0.1), iv(-0.1, 0.1), s);
    REQUIRE(onCurve == Sign::Uncertain);
    // a box far from the curve is AllFalse
    REQUIRE(r.classifyBox(iv(0.0, 0.1), iv(5.0, 6.0), s) == Sign::AllFalse);
}

TEST_CASE("point membership matches the relation", "[expr]")
{
    Relation r = must("x^2 + y^2 < 1");
    EvalScratch s;
    REQUIRE(r.pointInside(0.0, 0.0, s));
    REQUIRE_FALSE(r.pointInside(2.0, 0.0, s));

    Relation comp = must("x > 0 && y > 0");
    REQUIRE(comp.pointInside(1.0, 1.0, s));
    REQUIRE_FALSE(comp.pointInside(-1.0, 1.0, s));
}

TEST_CASE("undefined domains contribute no truth", "[expr]")
{
    Relation r = must("sqrt(x) > y");
    EvalScratch s;
    // x strictly negative -> sqrt undefined over the whole box
    REQUIRE(r.classifyBox(iv(-2.0, -1.0), iv(0.0, 1.0), s) == Sign::Undefined);
}

TEST_CASE("implicit multiplication parses like every real grapher", "[expr]")
{
    EvalScratch s;
    auto agree = [&](const std::string &a, const std::string &b) {
        std::string e1, e2;
        auto ra = Relation::parse(a, e1);
        auto rb = Relation::parse(b, e2);
        REQUIRE(ra.has_value());
        REQUIRE(rb.has_value());
        for (double x : {-2.3, -0.7, 0.4, 1.9})
            for (double y : {-1.6, 0.3, 2.2})
                REQUIRE(ra->pointInside(x, y, s) == rb->pointInside(x, y, s));
    };
    agree("x+y < 2x+y", "x+y < 2*x+y");          // the reported bug
    agree("y > 2x^2", "y > 2*(x^2)");            // implicit binds below ^
    agree("y < 2(x+1)", "y < 2*(x+1)");
    agree("xy > 1", "x*y > 1");
    agree("y = sin(x)cos(x)", "y = sin(x)*cos(x)");
    agree("y > (x+1)(x-1)", "y > (x+1)*(x-1)");
    agree("y > x(x - 2)", "y > x*(x - 2)");
    agree("y < 2pi", "y < 2*pi");
    agree("y > 2 - x", "y > 2-x");               // '-' never starts an implicit term

    std::string err;
    REQUIRE_FALSE(Relation::parse("y > zq", err).has_value()); // unknown ident still fails
}

TEST_CASE("functions apply without parentheses", "[expr]")
{
    EvalScratch s;
    auto agree = [&](const std::string &a, const std::string &b) {
        std::string e1, e2;
        auto ra = Relation::parse(a, e1);
        auto rb = Relation::parse(b, e2);
        REQUIRE(ra.has_value());
        REQUIRE(rb.has_value());
        for (double x : {-1.7, 0.3, 2.1})
            for (double y : {-0.9, 1.4})
                REQUIRE(ra->pointInside(x, y, s) == rb->pointInside(x, y, s));
    };
    agree("y > sin x", "y > sin(x)");
    agree("y > sin 2x", "y > sin(2*x)");
    agree("y = sin x cos x", "y = sin(x)*cos(x)");
    agree("y > sin x + 1", "y > sin(x) + 1");
    agree("y > sqrt x^2", "y > sqrt(x^2)");
}
