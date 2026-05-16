#ifndef STABLEHASH_H
#define STABLEHASH_H

#include <cstdint>
#include <string_view>

namespace gx
{
inline constexpr uint64_t FnvOffsetBasis = 14695981039346656037ull;
inline constexpr uint64_t FnvPrime = 1099511628211ull;

[[nodiscard]] inline uint64_t stableHash(const std::string_view value)
{
    auto hash = FnvOffsetBasis;
    for (const auto c : value)
    {
        hash ^= static_cast<unsigned char>(c);
        hash *= FnvPrime;
    }
    return hash;
}

[[nodiscard]] inline uint64_t combineStableHash(const uint64_t seed, const uint64_t value)
{
    auto hash = seed;
    for (auto i = 0; i < 8; ++i)
    {
        hash ^= static_cast<unsigned char>((value >> (i * 8)) & 0xffu);
        hash *= FnvPrime;
    }
    return hash;
}
}

#endif // STABLEHASH_H
