#ifndef GXR_TILE_TILEKEY_H
#define GXR_TILE_TILEKEY_H

#include "Geometry.h"

#include <cstdint>
#include <functional>

namespace gxr
{
// Address of a tile in the world-space power-of-two pyramid.
//   epoch : formula generation (bumped on relation change; old tiles kept for
//           fallback display until evicted).
//   level : worldPerPixel = 2^level.
//   i, j  : tile indices; tile spans world [i*S,(i+1)*S] x [j*S,(j+1)*S] with
//           S = tilePx * 2^level.
struct TileKey
{
    uint64_t epoch{0};
    int32_t level{0};
    int64_t i{0};
    int64_t j{0};

    bool operator==(const TileKey &) const = default;
};

[[nodiscard]] inline double tileSpanWorld(int level, int tilePx)
{
    return worldPerPixelAtLevel(level) * tilePx;
}

[[nodiscard]] inline WorldRect tileRect(const TileKey &k, int tilePx)
{
    const double s = tileSpanWorld(k.level, tilePx);
    return WorldRect{static_cast<double>(k.i) * s, static_cast<double>(k.j) * s,
                     static_cast<double>(k.i + 1) * s, static_cast<double>(k.j + 1) * s};
}

[[nodiscard]] inline int64_t floorDiv(double v, double s)
{
    return static_cast<int64_t>(std::floor(v / s));
}
}

template <>
struct std::hash<gxr::TileKey>
{
    size_t operator()(const gxr::TileKey &k) const noexcept
    {
        // splitmix-style mix of the four fields
        auto mix = [](uint64_t x) {
            x ^= x >> 30;
            x *= 0xbf58476d1ce4e5b9ULL;
            x ^= x >> 27;
            x *= 0x94d049bb133111ebULL;
            x ^= x >> 31;
            return x;
        };
        uint64_t h = mix(k.epoch);
        h ^= mix(static_cast<uint64_t>(static_cast<uint32_t>(k.level)) + 0x9e3779b9 + (h << 6) + (h >> 2));
        h ^= mix(static_cast<uint64_t>(k.i) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
        h ^= mix(static_cast<uint64_t>(k.j) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
        return static_cast<size_t>(h);
    }
};

#endif // GXR_TILE_TILEKEY_H
