// Headless renderer: formula + viewport -> coverage -> PNG. CPU-only.
// Usage: gxrender "<formula>" <out.png> [cx cy worldPerPixel size subBits]

#include "expr/Relation.h"
#include "image/Png.h"
#include "solve/Solver.h"
#include "tile/Geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace gxr;

namespace
{
double argd(int argc, char **argv, int i, double def)
{
    if (i >= argc) return def;
    try
    {
        return std::stod(argv[i]);
    }
    catch (...)
    {
        return def;
    }
}

int argi(int argc, char **argv, int i, int def)
{
    if (i >= argc) return def;
    try
    {
        return std::stoi(argv[i]);
    }
    catch (...)
    {
        return def;
    }
}

uint8_t toByte(double v) { return static_cast<uint8_t>(std::clamp(v, 0.0, 1.0) * 255.0 + 0.5); }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: gxrender \"<formula>\" <out.png> [cx cy wpp size subBits]\n";
        return 2;
    }
    const std::string formula = argv[1];
    const std::string outPath = argc >= 3 ? argv[2] : "out.png";
    const int size = argi(argc, argv, 6, 512);
    const double cx = argd(argc, argv, 3, 0.0);
    const double cy = argd(argc, argv, 4, 0.0);
    const double wpp = argd(argc, argv, 5, 8.0 / size); // default span ~[-4,4]
    const int subBits = argi(argc, argv, 7, 4);

    std::string err;
    auto rel = Relation::parse(formula, err);
    if (!rel)
    {
        std::cerr << "parse error: " << err << "\n";
        return 1;
    }

    Viewport vp{cx, cy, wpp, size, size};
    const WorldRect rect = vp.worldBounds();

    SolveParams p;
    p.tilePx = size;
    p.subBits = subBits;
    p.boxBudget = 4'000'000; // bounded; a 512px static render can afford more

    EvalScratch scratch;
    const CoverageTile tile = solveTile(*rel, rect, p, scratch);

    // colorize: dark background, blue fill blended by linear coverage; draw axes
    const std::array<double, 3> bg{0.07, 0.07, 0.09};
    const std::array<double, 3> fg{0.0, 0.55, 0.98};
    std::vector<uint8_t> rgba(static_cast<size_t>(size) * size * 4);

    for (int sy = 0; sy < size; ++sy)
    {
        // image row 0 = top = max world y; tile py 0 = min world y
        const int py = size - 1 - sy;
        const double worldY = rect.y0 + (py + 0.5) * rect.height() / size;
        for (int sx = 0; sx < size; ++sx)
        {
            const double a = tile.at(sx, py);
            double r = bg[0] + (fg[0] - bg[0]) * a;
            double g = bg[1] + (fg[1] - bg[1]) * a;
            double b = bg[2] + (fg[2] - bg[2]) * a;

            // axes
            const double worldX = rect.x0 + (sx + 0.5) * rect.width() / size;
            const bool onYAxis = std::abs(worldX) < 0.5 * rect.width() / size;
            const bool onXAxis = std::abs(worldY) < 0.5 * rect.height() / size;
            if (onXAxis || onYAxis)
            {
                r = std::max(r, 0.35);
                g = std::max(g, 0.35);
                b = std::max(b, 0.38);
            }

            const size_t idx = (static_cast<size_t>(sy) * size + sx) * 4;
            rgba[idx + 0] = toByte(r);
            rgba[idx + 1] = toByte(g);
            rgba[idx + 2] = toByte(b);
            rgba[idx + 3] = 255;
        }
    }

    if (!writePng(outPath, size, size, rgba))
    {
        std::cerr << "failed to write " << outPath << "\n";
        return 1;
    }
    std::cout << "wrote " << outPath << "  (converged=" << (tile.converged ? "yes" : "no")
              << ", worstUncertainty=" << tile.worstUncertainty << ")\n";
    return 0;
}
