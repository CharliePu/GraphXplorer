#ifndef GXR_TILE_GEOMETRY_H
#define GXR_TILE_GEOMETRY_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gxr
{
// An axis-aligned world-space rectangle (graph coordinates), x1>x0, y1>y0.
struct WorldRect
{
    double x0{0.0};
    double y0{0.0};
    double x1{0.0};
    double y1{0.0};

    [[nodiscard]] double width() const { return x1 - x0; }
    [[nodiscard]] double height() const { return y1 - y0; }
};

// The visible window: world center, scale (world units per screen pixel), and
// screen size in pixels.
struct Viewport
{
    double centerX{0.0};
    double centerY{0.0};
    double worldPerPixel{0.01}; // X-axis world-per-pixel
    int pxW{800};
    int pxH{800};
    // Anisotropic axis stretch: wppY = worldPerPixel * yStretch. The QUADTREE
    // stays square -- anisotropy is purely a view transform. Tiles solve at
    // the FINER axis (activeLevel below), so the stretched axis never loses
    // resolution; the compressed axis is merely supersampled. The app caps
    // the ratio (<= 8x) to bound the extra tile cost.
    double yStretch{1.0};

    [[nodiscard]] double wppX() const { return worldPerPixel; }
    [[nodiscard]] double wppY() const { return worldPerPixel * yStretch; }

    [[nodiscard]] WorldRect worldBounds() const
    {
        const double hw = 0.5 * wppX() * pxW;
        const double hh = 0.5 * wppY() * pxH;
        return {centerX - hw, centerY - hh, centerX + hw, centerY + hh};
    }

    // pyramid level whose tiles are ~1:1 with screen pixels on the FINER axis
    [[nodiscard]] int activeLevel() const
    {
        return static_cast<int>(std::lround(std::log2(std::min(wppX(), wppY()))));
    }
};

// worldPerPixel at a pyramid level (global power-of-two scale convention).
// ldexp is exact and far cheaper than exp2 on the per-node compositor path.
[[nodiscard]] inline double worldPerPixelAtLevel(int level) { return std::ldexp(1.0, level); }
}

#endif // GXR_TILE_GEOMETRY_H
