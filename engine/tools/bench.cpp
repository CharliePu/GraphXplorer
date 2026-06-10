// Throughput benchmark (the efficiency ground rule). CPU-only.
//  - single-thread: solve a screenful of fine tiles, report tiles/s and Mpix/s
//  - multi-thread: drive the Engine over a viewport, report wall time & speedup

#include "app/Engine.h"
#include "solve/Solver.h"
#include "tile/TileKey.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace gxr;
using clock_t_ = std::chrono::steady_clock;

namespace
{
std::shared_ptr<const Relation> rel(const std::string &src)
{
    std::string err;
    auto r = Relation::parse(src, err);
    if (!r)
    {
        std::printf("parse error (%s): %s\n", src.c_str(), err.c_str());
        std::exit(1);
    }
    return std::make_shared<const Relation>(std::move(*r));
}

double ms(clock_t_::time_point a, clock_t_::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}
}

int main()
{
    const std::vector<std::string> formulas{
        "y > sin(2^x)", "x^2 + y^2 < 1", "tan(x) > y", "sin(x*y) > 0", "y = x^2",
    };

    const int tilePx = 64;
    const int gridX = 8, gridY = 8; // ~512x512 screenful
    SolveParams fine{tilePx, 4, 200'000, true}; // bounded per-tile work

    std::printf("%-18s | %10s | %10s | %10s\n", "formula", "1T tiles/s", "1T Mpix/s", "engine ms");
    std::printf("-------------------+------------+------------+-----------\n");

    for (const std::string &src : formulas)
    {
        auto r = rel(src);
        EvalScratch scratch;

        // single-thread screenful (level 0 around origin)
        const int level = 0;
        const auto t0 = clock_t_::now();
        long long pixels = 0;
        for (int j = 0; j < gridY; ++j)
            for (int i = 0; i < gridX; ++i)
            {
                TileKey k{0, level, i - gridX / 2, j - gridY / 2};
                CoverageTile c = solveTile(*r, tileRect(k, tilePx), fine, scratch);
                pixels += static_cast<long long>(c.width) * c.height;
            }
        const auto t1 = clock_t_::now();
        const double secs = ms(t0, t1) / 1000.0;
        const double tilesPerSec = (gridX * gridY) / secs;
        const double mpixPerSec = pixels / secs / 1e6;

        // multi-thread engine: time to fully solve the same viewport
        Engine engine(tilePx);
        engine.setRelation(r);
        Viewport vp{0.0, 0.0, worldPerPixelAtLevel(level), gridX * tilePx, gridY * tilePx};
        const auto e0 = clock_t_::now();
        engine.setViewport(vp);
        engine.waitUntilQuiescent();
        const auto e1 = clock_t_::now();

        std::printf("%-18s | %10.1f | %10.1f | %9.1f\n", src.c_str(), tilesPerSec, mpixPerSec,
                    ms(e0, e1));
    }
    return 0;
}
