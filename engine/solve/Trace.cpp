#include "Trace.h"

#include <algorithm>
#include <cmath>

namespace gxr
{
TraceHit traceCurve(const Relation &rel, double cursorX, double cursorY, double wppX,
                    double wppY, EvalScratch &scratch, double reachPx, const TraceHit *prev)
{
    TraceHit hit;
    const double wppM = std::max(wppX, wppY);
    const double reach = reachPx * wppM; // stay local to the cursor
    // distance tolerance: a millionth of a pixel, floored at the ulp scale of
    // the coordinates so deep zooms (wpp ~ 1e-12) still converge
    auto tolD = [&](double x, double y) {
        return std::max(wppM * 1e-6, (std::abs(x) + std::abs(y) + 1.0) * 1e-14);
    };

    // Newton glide: q <- q - f(q) grad/|grad|^2. Converges to (approximately)
    // the foot point on the curve for well-conditioned f.
    auto newtonGlide = [&](double sx, double sy, int iters) {
        double nx = sx, ny = sy;
        for (int it = 0; it < iters; ++it)
        {
            Interval v, gx, gy;
            rel.valueAndGrad(Interval{nx, nx}, Interval{ny, ny}, scratch, v, gx, gy);
            if (v.undef) return false;
            const double f = v.mid(), dx = gx.mid(), dy = gy.mid();
            const double g2 = dx * dx + dy * dy;
            if (!std::isfinite(f) || !(g2 > 1e-300)) return false;
            if (it >= 1 && std::abs(f) <= std::sqrt(g2) * tolD(nx, ny))
            {
                hit.x = nx;
                hit.y = ny;
                return true;
            }
            const double step = f / g2;
            nx -= step * dx;
            ny -= step * dy;
            if (!std::isfinite(nx) || !std::isfinite(ny)) return false;
            if (std::abs(nx - cursorX) > reach || std::abs(ny - cursorY) > reach) return false;
        }
        return false;
    };

    // Axis bracket: the sign change of f nearest the cursor along one axis,
    // bisected to ~1 ulp. A sign flip across a POLE bisects to the asymptote
    // with |f| exploding instead of vanishing -- rejected by the residual
    // check (y = 1/x must not pin a marker onto x = 0).
    auto axisSeed = [&](bool vertical, double &ox, double &oy) {
        const double span = 18.0 * (vertical ? wppY : wppX);
        constexpr int N = 24;
        auto at = [&](double t) {
            return vertical ? rel.fValue(cursorX, t, scratch) : rel.fValue(t, cursorY, scratch);
        };
        const double c0 = vertical ? cursorY : cursorX;
        double pt = c0 - span, pf = at(pt);
        double bLo = 0, bHi = 0, bD = 1e300;
        bool found = false;
        for (int i = 1; i <= N; ++i)
        {
            const double tt = c0 - span + 2.0 * span * i / N;
            const double ff = at(tt);
            if (std::isfinite(pf) && std::isfinite(ff) && (pf < 0.0) != (ff < 0.0))
            {
                const double d = std::abs(0.5 * (pt + tt) - c0);
                if (d < bD)
                {
                    bD = d;
                    bLo = pt;
                    bHi = tt;
                    found = true;
                }
            }
            pt = tt;
            pf = ff;
        }
        if (!found) return 1e300;
        double lo = bLo, hi = bHi, flo = at(lo);
        for (int it = 0; it < 60; ++it)
        {
            const double mid = 0.5 * (lo + hi);
            const double fm = at(mid);
            if (!std::isfinite(fm)) break;
            if ((fm < 0.0) == (flo < 0.0))
            {
                lo = mid;
                flo = fm;
            }
            else hi = mid;
        }
        const double root = 0.5 * (lo + hi);
        const double fr = at(root);
        const double fEnds = std::abs(at(bLo)) + std::abs(at(bHi));
        if (!std::isfinite(fr) || !(std::abs(fr) <= fEnds * 1e-6 + 1e-300)) return 1e300;
        ox = vertical ? cursorX : root;
        oy = vertical ? root : cursorY;
        return bD;
    };

    // BRANCH CONTINUITY: walk the previously-held branch toward the cursor.
    // Each step moves along the local tangent (never further than ~2.5 px,
    // so the walk cannot tunnel across a neighboring strand) and Newton-
    // reprojects onto the curve. The hit stays on ITS line until the cursor
    // genuinely leaves the reach.
    if (prev && prev->traced && std::isfinite(prev->x) && std::isfinite(prev->y) &&
        std::abs(prev->x - cursorX) <= reach * 2.0 && std::abs(prev->y - cursorY) <= reach * 2.0)
    {
        double qx = prev->x, qy = prev->y;
        const double stepMax = 2.5 * wppM;
        const double need = std::hypot(cursorX - qx, cursorY - qy);
        const int iters = std::clamp(static_cast<int>(need / stepMax) + 4, 6, 48);
        bool ok = false;
        for (int it = 0; it < iters; ++it)
        {
            Interval v, gx, gy;
            rel.valueAndGrad(Interval{qx, qx}, Interval{qy, qy}, scratch, v, gx, gy);
            if (v.undef) break;
            const double f = v.mid(), dx = gx.mid(), dy = gy.mid();
            const double g2 = dx * dx + dy * dy;
            if (!std::isfinite(f) || !(g2 > 1e-300)) break;
            // tangential pull toward the cursor, then reproject
            const double inv = 1.0 / std::sqrt(g2);
            const double nx = dx * inv, ny = dy * inv;
            double tx = (cursorX - qx), ty = (cursorY - qy);
            const double along = tx * nx + ty * ny;
            tx -= along * nx;
            ty -= along * ny;
            const double tl = std::hypot(tx, ty);
            if (tl > stepMax)
            {
                tx *= stepMax / tl;
                ty *= stepMax / tl;
            }
            qx += tx - f * dx / g2;
            qy += ty - f * dy / g2;
            if (!std::isfinite(qx) || !std::isfinite(qy)) break;
            if (tl <= 0.05 * wppM && std::abs(f) <= std::sqrt(g2) * tolD(qx, qy))
            {
                ok = true;
                break;
            }
        }
        if (ok)
        {
            // accept only while the branch point still serves the cursor
            if (std::hypot(qx - cursorX, qy - cursorY) <= reach * 1.5)
            {
                hit.x = qx;
                hit.y = qy;
                hit.traced = true;
            }
        }
        if (hit.traced) goto certify;
    }

    // FRESH search: refuse dense neighborhoods -- if the axis scans see many
    // strands inside the window, this is a field; panning must win the click.
    if (!prev)
    {
        int crossings = 0;
        for (int axis = 0; axis < 2; ++axis)
        {
            const double span = 18.0 * (axis == 0 ? wppY : wppX);
            const double c0 = axis == 0 ? cursorY : cursorX;
            double pf = axis == 0 ? rel.fValue(cursorX, c0 - span, scratch)
                                  : rel.fValue(c0 - span, cursorY, scratch);
            for (int i = 1; i <= 24; ++i)
            {
                const double tt = c0 - span + 2.0 * span * i / 24;
                const double ff = axis == 0 ? rel.fValue(cursorX, tt, scratch)
                                            : rel.fValue(tt, cursorY, scratch);
                if (std::isfinite(pf) && std::isfinite(ff) && (pf < 0.0) != (ff < 0.0))
                    ++crossings;
                pf = ff;
            }
        }
        if (crossings >= 5) return hit; // a thicket: trace declines, pan wins
    }

    hit.traced = newtonGlide(cursorX, cursorY, 14);
    if (!hit.traced)
    {
        double vx, vy, hx, hy;
        const double dv = axisSeed(true, vx, vy);
        const double dh = axisSeed(false, hx, hy);
        if (dv < 1e300 || dh < 1e300)
        {
            hit.x = dv <= dh ? vx : hx;
            hit.y = dv <= dh ? vy : hy;
            hit.traced = true;
            newtonGlide(hit.x, hit.y, 6); // polish toward the foot point
        }
    }
    if (!hit.traced) return hit;

certify:
    // Certificate: on [hit +- d], one gradient component is bounded off zero
    // AND the two edge slabs across that axis carry opposite PROVEN signs ->
    // by IVT a crossing lies inside the box. The first box is sized to the
    // convergence tolerance (the true root lies within it); growth handles
    // interval overestimation at tiny widths.
    double d = 4.0 * tolD(hit.x, hit.y) + wppM * 1e-9;
    for (int t = 0; t < 4 && !hit.certified; ++t, d *= 32.0)
    {
        const Interval bx{hit.x - d, hit.x + d}, by{hit.y - d, hit.y + d};
        Interval v, gx, gy;
        rel.valueAndGrad(bx, by, scratch, v, gx, gy);
        if (v.undef || v.disc || !v.straddlesZero()) break;
        // slab axis = the DOMINANT bounded gradient component: the slab's
        // sign must survive the other axis's variation across the box
        Interval eA, eB, ga, gb;
        const bool yOk = !gy.straddlesZero(), xOk = !gx.straddlesZero();
        const bool useY = yOk && (!xOk || std::abs(gy.mid()) >= std::abs(gx.mid()));
        if (useY)
        {
            rel.valueAndGrad(bx, Interval{hit.y - d, hit.y - d}, scratch, eA, ga, gb);
            rel.valueAndGrad(bx, Interval{hit.y + d, hit.y + d}, scratch, eB, ga, gb);
        }
        else if (xOk)
        {
            rel.valueAndGrad(Interval{hit.x - d, hit.x - d}, by, scratch, eA, ga, gb);
            rel.valueAndGrad(Interval{hit.x + d, hit.x + d}, by, scratch, eB, ga, gb);
        }
        else continue;
        if (eA.undef || eB.undef) break;
        hit.certified = (eA.hi < 0.0 && eB.lo > 0.0) || (eA.lo > 0.0 && eB.hi < 0.0);
    }
    return hit;
}
}
