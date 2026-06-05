#ifndef GXR_EXPR_PARSER_H
#define GXR_EXPR_PARSER_H

#include "Ast.h"

#include <memory>
#include <optional>
#include <string>

namespace gxr
{
struct ParseResult
{
    std::unique_ptr<Node> root;
    bool ok{false};
    std::string error;
};

// Recursive-descent / precedence-climbing parser for implicit-relation
// expressions. Variables x and y map to slots 0 and 1. Constants pi and e are
// recognized. Supported: + - * / ^, unary -, sin cos tan log exp sqrt abs,
// comparisons (< <= > >= = == !=) and && ||.
[[nodiscard]] ParseResult parseExpression(const std::string &text);
}

#endif // GXR_EXPR_PARSER_H
