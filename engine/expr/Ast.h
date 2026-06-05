#ifndef GXR_EXPR_AST_H
#define GXR_EXPR_AST_H

#include <memory>
#include <string>
#include <vector>

namespace gxr
{
enum class NodeKind
{
    Const,
    Var, // slot 0 = x, slot 1 = y
    Neg,
    Add,
    Sub,
    Mul,
    Div,
    Pow,
    Abs,
    Sin,
    Cos,
    Tan,
    Asin,
    Acos,
    Atan,
    Log,
    Exp,
    Sqrt,
    // relational / logical (truth-valued)
    Less,
    LessEq,
    Greater,
    GreaterEq,
    Equal,
    NotEqual,
    And,
    Or,
};

struct Node
{
    NodeKind kind{NodeKind::Const};
    double value{0.0};   // for Const
    int slot{0};         // for Var
    std::unique_ptr<Node> a;
    std::unique_ptr<Node> b;
};

[[nodiscard]] inline bool isComparison(NodeKind k)
{
    switch (k)
    {
    case NodeKind::Less:
    case NodeKind::LessEq:
    case NodeKind::Greater:
    case NodeKind::GreaterEq:
    case NodeKind::Equal:
    case NodeKind::NotEqual:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool isLogical(NodeKind k)
{
    return k == NodeKind::And || k == NodeKind::Or;
}
}

#endif // GXR_EXPR_AST_H
