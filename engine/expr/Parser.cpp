#include "Parser.h"

#include <cctype>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace gxr
{
namespace
{
struct ParseError
{
    std::string message;
};

[[noreturn]] void fail(const std::string &m) { throw ParseError{m}; }

std::unique_ptr<Node> makeConst(double v)
{
    auto n = std::make_unique<Node>();
    n->kind = NodeKind::Const;
    n->value = v;
    return n;
}

std::unique_ptr<Node> makeUnary(NodeKind k, std::unique_ptr<Node> a)
{
    auto n = std::make_unique<Node>();
    n->kind = k;
    n->a = std::move(a);
    return n;
}

std::unique_ptr<Node> makeBinary(NodeKind k, std::unique_ptr<Node> a, std::unique_ptr<Node> b)
{
    auto n = std::make_unique<Node>();
    n->kind = k;
    n->a = std::move(a);
    n->b = std::move(b);
    return n;
}

class Parser
{
public:
    explicit Parser(std::string_view src) : s{src} {}

    std::unique_ptr<Node> parse()
    {
        auto root = parseOr();
        skipWs();
        if (pos != s.size())
        {
            fail("unexpected trailing characters at " + std::to_string(pos));
        }
        return root;
    }

private:
    std::string_view s;
    size_t pos{0};

    void skipWs()
    {
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
    }

    bool peekIs(std::string_view tok)
    {
        skipWs();
        return s.substr(pos, tok.size()) == tok;
    }

    bool accept(std::string_view tok)
    {
        if (peekIs(tok))
        {
            pos += tok.size();
            return true;
        }
        return false;
    }

    // or := and ('||' and)*
    std::unique_ptr<Node> parseOr()
    {
        auto lhs = parseAnd();
        while (accept("||"))
        {
            lhs = makeBinary(NodeKind::Or, std::move(lhs), parseAnd());
        }
        return lhs;
    }

    // and := cmp ('&&' cmp)*
    std::unique_ptr<Node> parseAnd()
    {
        auto lhs = parseCmp();
        while (accept("&&"))
        {
            lhs = makeBinary(NodeKind::And, std::move(lhs), parseCmp());
        }
        return lhs;
    }

    // cmp := add (relop add)?   (single, non-chained)
    std::unique_ptr<Node> parseCmp()
    {
        auto lhs = parseAdd();
        // order matters: match two-char ops before one-char
        if (accept("<=")) return makeBinary(NodeKind::LessEq, std::move(lhs), parseAdd());
        if (accept(">=")) return makeBinary(NodeKind::GreaterEq, std::move(lhs), parseAdd());
        if (accept("==")) return makeBinary(NodeKind::Equal, std::move(lhs), parseAdd());
        if (accept("!=")) return makeBinary(NodeKind::NotEqual, std::move(lhs), parseAdd());
        if (accept("<")) return makeBinary(NodeKind::Less, std::move(lhs), parseAdd());
        if (accept(">")) return makeBinary(NodeKind::Greater, std::move(lhs), parseAdd());
        if (accept("=")) return makeBinary(NodeKind::Equal, std::move(lhs), parseAdd());
        return lhs;
    }

    // add := mul (('+'|'-') mul)*
    std::unique_ptr<Node> parseAdd()
    {
        auto lhs = parseMul();
        for (;;)
        {
            skipWs();
            if (accept("+")) lhs = makeBinary(NodeKind::Add, std::move(lhs), parseMul());
            else if (accept("-")) lhs = makeBinary(NodeKind::Sub, std::move(lhs), parseMul());
            else break;
        }
        return lhs;
    }

    // mul := unary (('*'|'/') unary)*
    std::unique_ptr<Node> parseMul()
    {
        auto lhs = parseUnary();
        for (;;)
        {
            skipWs();
            if (accept("*")) lhs = makeBinary(NodeKind::Mul, std::move(lhs), parseUnary());
            else if (accept("/")) lhs = makeBinary(NodeKind::Div, std::move(lhs), parseUnary());
            else break;
        }
        return lhs;
    }

    // unary := ('-'|'+') unary | pow
    std::unique_ptr<Node> parseUnary()
    {
        skipWs();
        if (accept("-")) return makeUnary(NodeKind::Neg, parseUnary());
        if (accept("+")) return parseUnary();
        return parsePow();
    }

    // pow := primary ('^' unary)?   (right associative)
    std::unique_ptr<Node> parsePow()
    {
        auto base = parsePrimary();
        skipWs();
        if (accept("^"))
        {
            return makeBinary(NodeKind::Pow, std::move(base), parseUnary());
        }
        return base;
    }

    std::unique_ptr<Node> parsePrimary()
    {
        skipWs();
        if (pos >= s.size()) fail("unexpected end of input");

        if (accept("("))
        {
            auto inner = parseOr();
            if (!accept(")")) fail("missing ')'");
            return inner;
        }

        const char c = s[pos];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.')
        {
            return parseNumber();
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
        {
            return parseIdent();
        }
        fail(std::string("unexpected character '") + c + "'");
    }

    std::unique_ptr<Node> parseNumber()
    {
        const size_t start = pos;
        while (pos < s.size()
               && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.'))
        {
            ++pos;
        }
        // exponent
        if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E'))
        {
            size_t look = pos + 1;
            if (look < s.size() && (s[look] == '+' || s[look] == '-')) ++look;
            if (look < s.size() && std::isdigit(static_cast<unsigned char>(s[look])))
            {
                pos = look;
                while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
            }
        }
        const std::string num{s.substr(start, pos - start)};
        try
        {
            return makeConst(std::stod(num));
        }
        catch (...)
        {
            fail("invalid number '" + num + "'");
        }
    }

    std::unique_ptr<Node> parseIdent()
    {
        const size_t start = pos;
        while (pos < s.size()
               && (std::isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_'))
        {
            ++pos;
        }
        const std::string name{s.substr(start, pos - start)};

        // function call?
        skipWs();
        if (pos < s.size() && s[pos] == '(')
        {
            ++pos; // consume '('
            auto arg = parseOr();
            if (!accept(")")) fail("missing ')' after function argument");
            static const std::unordered_map<std::string, NodeKind> fns{
                {"sin", NodeKind::Sin},   {"cos", NodeKind::Cos},   {"tan", NodeKind::Tan},
                {"asin", NodeKind::Asin}, {"acos", NodeKind::Acos}, {"atan", NodeKind::Atan},
                {"log", NodeKind::Log},   {"ln", NodeKind::Log},    {"exp", NodeKind::Exp},
                {"sqrt", NodeKind::Sqrt}, {"abs", NodeKind::Abs}};
            const auto it = fns.find(name);
            if (it == fns.end()) fail("unknown function '" + name + "'");
            return makeUnary(it->second, std::move(arg));
        }

        if (name == "x")
        {
            auto n = std::make_unique<Node>();
            n->kind = NodeKind::Var;
            n->slot = 0;
            return n;
        }
        if (name == "y")
        {
            auto n = std::make_unique<Node>();
            n->kind = NodeKind::Var;
            n->slot = 1;
            return n;
        }
        if (name == "pi") return makeConst(std::numbers::pi);
        if (name == "e") return makeConst(std::numbers::e);
        fail("unknown identifier '" + name + "'");
    }
};
}

ParseResult parseExpression(const std::string &text)
{
    ParseResult r;
    try
    {
        Parser p{text};
        r.root = p.parse();
        r.ok = true;
    }
    catch (const ParseError &e)
    {
        r.ok = false;
        r.error = e.message;
    }
    catch (const std::exception &e)
    {
        r.ok = false;
        r.error = e.what();
    }
    return r;
}
}
