#ifndef GXR_EXPR_PARSER_H
#define GXR_EXPR_PARSER_H

#include <unordered_map>
#include <vector>
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
// `params`: values for single-letter parameters (lowercase letters other
// than x, y, e); letters missing from the table read as 1.0. `usedParams`
// collects the letters the text referenced, in first-use order, so the UI
// can grow a slider per parameter.
[[nodiscard]] ParseResult parseExpression(
    const std::string &text, const std::unordered_map<char, double> *params = nullptr,
    std::vector<char> *usedParams = nullptr);
}

#endif // GXR_EXPR_PARSER_H
