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

    // mul := unary (('*'|'/') unary | implicit)*
    // Implicit multiplication: any adjacent primary-start continues the
    // product -- 2x, 2(x+1), x y, sin(x)cos(x), (a)(b), 2pi. The implicit
    // operand is a POW term (2x^2 == 2*(x^2)), and '-' never starts one, so
    // "2 - x" stays a subtraction.
    std::unique_ptr<Node> parseMul()
    {
        auto lhs = parseUnary();
        for (;;)
        {
            skipWs();
            if (accept("*")) lhs = makeBinary(NodeKind::Mul, std::move(lhs), parseUnary());
            else if (accept("/")) lhs = makeBinary(NodeKind::Div, std::move(lhs), parseUnary());
            else if (pos < s.size() &&
                     (std::isalpha(static_cast<unsigned char>(s[pos])) || s[pos] == '_' ||
                      std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.' ||
                      s[pos] == '('))
            {
                lhs = makeBinary(NodeKind::Mul, std::move(lhs), parsePow());
            }
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

    bool nextIsFunctionName()
    {
        size_t p2 = pos;
        while (p2 < s.size() && std::isspace(static_cast<unsigned char>(s[p2]))) ++p2;
        if (p2 >= s.size() ||
            !(std::isalpha(static_cast<unsigned char>(s[p2])) || s[p2] == '_'))
            return false;
        size_t e2 = p2;
        while (e2 < s.size() &&
               (std::isalnum(static_cast<unsigned char>(s[e2])) || s[e2] == '_'))
            ++e2;
        static constexpr std::string_view names[] = {"sin",   "cos",  "tan",  "asin", "acos",
                                                     "atan",  "log",  "ln",   "exp",  "sqrt",
                                                     "abs",   "floor", "ceil", "sign", "min",
                                                     "max"};
        const std::string_view nm = s.substr(p2, e2 - p2);
        for (const std::string_view n2 : names)
            if (nm == n2) return true;
        return false;
    }

    // Function argument without parentheses: an implicit product of pow-terms
    // that stops before the next function name or operator --
    // sin 2x == sin(2x), sin x cos x == sin(x)*cos(x), sin x + 1 == sin(x)+1.
    std::unique_ptr<Node> parseFnArg()
    {
        auto term = parsePow();
        for (;;)
        {
            skipWs();
            if (pos < s.size() &&
                (std::isalpha(static_cast<unsigned char>(s[pos])) || s[pos] == '_' ||
                 std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.' ||
                 s[pos] == '('))
            {
                if (nextIsFunctionName()) break;
                term = makeBinary(NodeKind::Mul, std::move(term), parsePow());
            }
            else break;
        }
        return term;
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

        // two-argument functions: min(a, b), max(a, b)
        if (name == "min" || name == "max")
        {
            skipWs();
            if (!accept("(")) fail("'" + name + "' needs parentheses: " + name + "(a, b)");
            auto a1 = parseOr();
            if (!accept(",")) fail("'" + name + "' takes two arguments: " + name + "(a, b)");
            auto a2 = parseOr();
            if (!accept(")")) fail("missing ')' after " + name + " arguments");
            return makeBinary(name == "min" ? NodeKind::Min : NodeKind::Max, std::move(a1),
                              std::move(a2));
        }
        static const std::unordered_map<std::string, NodeKind> fns{
            {"sin", NodeKind::Sin},    {"cos", NodeKind::Cos},   {"tan", NodeKind::Tan},
            {"asin", NodeKind::Asin},  {"acos", NodeKind::Acos}, {"atan", NodeKind::Atan},
            {"log", NodeKind::Log},    {"ln", NodeKind::Log},    {"exp", NodeKind::Exp},
            {"sqrt", NodeKind::Sqrt},  {"abs", NodeKind::Abs},   {"floor", NodeKind::Floor},
            {"ceil", NodeKind::Ceil},  {"sign", NodeKind::Sign}};
        const auto it = fns.find(name);
        if (it != fns.end())
        {
            skipWs();
            if (pos < s.size() && s[pos] == '(')
            {
                ++pos; // consume '('
                auto arg = parseOr();
                if (!accept(")")) fail("missing ')' after function argument");
                return makeUnary(it->second, std::move(arg));
            }
            return makeUnary(it->second, parseFnArg()); // sin x, sin 2x, ...
        }
        // a non-function name followed by '(' is an implicit product: x(x+1)

        auto makeVar = [](int slot) {
            auto n = std::make_unique<Node>();
            n->kind = NodeKind::Var;
            n->slot = slot;
            return n;
        };
        if (name == "x") return makeVar(0);
        if (name == "y") return makeVar(1);
        if (name == "pi") return makeConst(std::numbers::pi);
        if (name == "e") return makeConst(std::numbers::e);

        // PARAMETERS: any other lowercase letter is a free constant whose
        // value comes from the caller's table (1.0 until a slider exists).
        // Letter runs split into products (xy -> x*y, ax -> a*x, ab -> a*b)
        // -- except when a function name hides inside: xsinx stays an error
        // instead of silently becoming x*s*i*n*x.
        auto letterFactor = [&](char ch) -> std::unique_ptr<Node> {
            if (ch == 'x') return makeVar(0);
            if (ch == 'y') return makeVar(1);
            if (ch == 'e') return makeConst(std::numbers::e);
            double v = 1.0;
            if (params)
            {
                const auto pit = params->find(ch);
                if (pit != params->end()) v = pit->second;
            }
            if (used)
            {
                bool seen = false;
                for (const char u : *used) seen = seen || u == ch;
                if (!seen) used->push_back(ch);
            }
            return makeConst(v);
        };
        bool allLower = !name.empty();
        for (const char ch : name) allLower = allLower && ch >= 'a' && ch <= 'z';
        if (allLower)
        {
            static const char *kFnNames[] = {"asin", "acos", "atan", "sin",   "cos",
                                             "tan",  "log",  "ln",   "exp",   "sqrt",
                                             "abs",  "min",  "max",  "floor", "ceil",
                                             "sign", "pi"};
            bool hidesFn = false;
            for (const char *fn : kFnNames)
                hidesFn = hidesFn || name.find(fn) != std::string::npos;
            if (!hidesFn)
            {
                auto prod = letterFactor(name[0]);
                for (size_t i = 1; i < name.size(); ++i)
                    prod = makeBinary(NodeKind::Mul, std::move(prod), letterFactor(name[i]));
                return prod;
            }
        }
        fail("unknown identifier '" + name + "'");
    }

public:
    const std::unordered_map<char, double> *params{nullptr};
    std::vector<char> *used{nullptr};
};
}

ParseResult parseExpression(const std::string &text,
                            const std::unordered_map<char, double> *params,
                            std::vector<char> *usedParams)
{
    ParseResult r;
    try
    {
        Parser p{text};
        p.params = params;
        p.used = usedParams;
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
