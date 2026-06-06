// Repro harness for the "maximize-then-zoom-out -> tiles not generated" bug.
// Drives the Engine through small -> maximize -> zoom-out viewport changes and
// reports, after each settles, how many on-screen leaves are still FALLBACK
// (i.e. not generated). A nonzero FALLBACK after quiescence == bug reproduced.

#include "app/Engine.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace gxr;
using clk = std::chrono::steady_clock;

namespace
{
std::shared_ptr<const Relation> rel(const std::string &src)
{
    std::string err;
    auto r = Relation::parse(src, err);
    if (!r)
    {
        std::printf("parse error: %s\n", err.c_str());
        std::exit(1);
    }
    return std::make_shared<const Relation>(std::move(*r));
}

double ms(clk::time_point a, clk::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}
}

int main()
{
    Engine engine(64, 4);
    engine.setRelation(rel("y > sin(2^x)"));

    std::vector<PresentTile> present;
    auto report = [&](const char *label, Viewport vp) {
        const auto t0 = clk::now();
        engine.setViewport(vp);
        size_t n = 0;
        int fb = 0, done = 0, flat = 0, other = 0, iters = 0;
        // buildPresent may request re-solves of stuck tiles; settle until stable.
        for (iters = 1; iters <= 8; ++iters)
        {
            engine.waitUntilQuiescent();
            n = engine.buildPresent(vp, present);
            fb = done = flat = other = 0;
            for (const PresentTile &p : present)
            {
                if (p.fallback) ++fb;
                else if (p.flat) ++flat;
                else if (p.state == TileState::Done) ++done;
                else ++other;
            }
            if (fb == 0) break;
        }
        const auto t1 = clk::now();
        std::printf(
            "%-10s vp=%4dx%-4d wpp=%-7.4g | present=%-4zu flat=%-4d done=%-4d FALLBACK=%-4d"
            " | iters=%d settleMs=%6.1f store=%zu\n",
            label, vp.pxW, vp.pxH, vp.worldPerPixel, n, flat, done, fb, iters, ms(t0, t1),
            engine.storeSize());
    };

    report("small", Viewport{0.0, 0.0, 0.01, 400, 300});
    report("maximize", Viewport{0.0, 0.0, 0.01, 1280, 720});
    report("zoomout", Viewport{0.0, 0.0, 0.04, 1280, 720});
    report("zoomout2", Viewport{0.0, 0.0, 0.16, 1280, 720});
    report("zoomout3", Viewport{0.0, 0.0, 0.64, 1280, 720});
    std::printf("(FALLBACK>0 after a settle == tiles not generated == bug reproduced)\n");
    return 0;
}
