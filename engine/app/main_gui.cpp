// GraphXplorer - live CPU renderer. Pan = drag, zoom = scroll, 1-6 = preset
// formulas, R = reset view, Esc = quit. All GL on the main thread; all math async.

#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <glfwpp/glfwpp.h>

#include "app/Engine.h"
#include "image/Png.h"
#include "present/GlPresenter.h"
#include "present/Overlay.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace gxr;

namespace
{
std::shared_ptr<const Relation> parseOrNull(const std::string &src)
{
    std::string err;
    auto r = Relation::parse(src, err);
    if (!r)
    {
        std::fprintf(stderr, "parse error (%s): %s\n", src.c_str(), err.c_str());
        return nullptr;
    }
    return std::make_shared<const Relation>(std::move(*r));
}

const char *kPresets[] = {
    "y > sin(2^x)", "x^2 + y^2 < 1", "y = x^2", "tan(x) > y", "sin(x*y) > 0", "y = x^3 - x",
};

void saveFramebuffer(int fbW, int fbH, const std::string &path)
{
    std::vector<uint8_t> px(static_cast<size_t>(fbW) * fbH * 4);
    glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    std::vector<uint8_t> flipped(px.size()); // GL bottom-up -> PNG top-down
    for (int y = 0; y < fbH; ++y)
        std::memcpy(&flipped[static_cast<size_t>(fbH - 1 - y) * fbW * 4],
                    &px[static_cast<size_t>(y) * fbW * 4], static_cast<size_t>(fbW) * 4);
    (void)writePng(path, fbW, fbH, flipped);
}

std::string findFont()
{
#ifdef GXR_ASSET_DIR
    const std::string a = std::string(GXR_ASSET_DIR) + "/font/FiraCode-Regular.ttf";
    if (std::filesystem::exists(a)) return a;
#endif
    return "font/FiraCode-Regular.ttf";
}

// Draw the on-screen UI: a formula bar (editable, with a positionable caret),
// a status line, and a help bar.
void drawUi(Overlay &ui, int fbW, int fbH, const std::string &formula, bool editing,
           const std::string &edit, size_t editPos, const std::string &status)
{
    if (!ui.ok()) return;
    ui.begin();
    const float w = static_cast<float>(fbW), h = static_cast<float>(fbH);

    // top formula bar
    ui.fillRect(0, 0, w, 40, {0.05f, 0.05f, 0.08f, 0.86f});
    const std::string shown = editing ? ("f:  " + edit) : ("f:  " + formula);
    const std::array<float, 4> fcol = editing ? std::array<float, 4>{1.0f, 0.92f, 0.55f, 1.0f}
                                              : std::array<float, 4>{0.88f, 0.94f, 1.0f, 1.0f};
    ui.text(14, 9, shown, 1.0f, fcol);
    if (editing)
    {
        // caret at the edit position (insertion happens mid-string)
        const std::string prefix = "f:  " + edit.substr(0, std::min(editPos, edit.size()));
        const float cx = 14.0f + ui.textWidth(prefix, 1.0f) + 1.0f;
        ui.fillRect(cx, 8, 2, 24, fcol);
    }

    // status (e.g. parse error)
    if (!status.empty())
        ui.text(14, 46, status, 0.82f, {1.0f, 0.5f, 0.5f, 1.0f});

    // bottom help bar
    ui.fillRect(0, h - 28, w, 28, {0.05f, 0.05f, 0.08f, 0.82f});
    const std::string help =
        "drag pan   scroll zoom   [Enter] edit   [1-6] presets   [D] debug   [R] reset   [Esc] quit";
    ui.text(14, h - 23, help, 0.78f, {0.66f, 0.72f, 0.84f, 1.0f});
}

// Debug overlay: visible tiles outlined and coloured by solve state, plus an
// info panel (fps, level, viewport, tile/store/job counts, frame-latency
// attribution) and a legend.
void drawDebug(Overlay &ui, const Viewport &vp, int fbW, int fbH,
              const std::vector<DebugTile> &tiles, size_t storeSize, unsigned long long jobs,
              double fps, double frameMs, int holes, const char *perf1 = nullptr,
              const char *perf2 = nullptr)
{
    if (!ui.ok()) return;
    ui.begin();
    const double cx = vp.centerX, cy = vp.centerY, wpp = vp.worldPerPixel;
    auto sx = [&](double wx) { return (wx - cx) / wpp + fbW * 0.5; };
    auto syTop = [&](double wy) { return fbH * 0.5 - (wy - cy) / wpp; };

    for (const DebugTile &t : tiles)
    {
        const float x = static_cast<float>(sx(t.rect.x0));
        const float top = static_cast<float>(syTop(t.rect.y1));
        const float tw = static_cast<float>((t.rect.x1 - t.rect.x0) / wpp);
        const float th = static_cast<float>((t.rect.y1 - t.rect.y0) / wpp);
        std::array<float, 4> c;
        switch (t.state)
        {
        case TileState::Done: c = {0.25f, 0.9f, 0.45f, 0.5f}; break;
        case TileState::Coarse: c = {0.98f, 0.82f, 0.2f, 0.6f}; break;
        case TileState::Queued: c = {1.0f, 0.5f, 0.15f, 0.65f}; break;
        default: c = {1.0f, 0.3f, 0.3f, 0.55f}; break;
        }
        ui.rectOutline(x, top, tw, th, 1.0f, c);
    }

    // info panel (top-right)
    const float pw = 430, ph = perf1 ? 296 : 206, px = fbW - pw - 10, py = 50;
    ui.fillRect(px, py, pw, ph, {0.04f, 0.04f, 0.06f, 0.88f});
    const std::array<float, 4> tc{0.85f, 0.9f, 1.0f, 1.0f};
    float ty = py + 8;
    char buf[160];
    auto line = [&](const char *s) {
        ui.text(px + 12, ty, s, 0.8f, tc);
        ty += ui.lineHeight(0.8f) + 2;
    };
    std::snprintf(buf, sizeof buf, "fps %.0f    frame %.1f ms", fps, frameMs);
    line(buf);
    std::snprintf(buf, sizeof buf, "level %d    wpp %.4g", vp.activeLevel(), wpp);
    line(buf);
    std::snprintf(buf, sizeof buf, "center  %.4g , %.4g", cx, cy);
    line(buf);
    std::snprintf(buf, sizeof buf, "visible %zu    store %zu", tiles.size(), storeSize);
    line(buf);
    std::snprintf(buf, sizeof buf, "jobs done %llu", jobs);
    line(buf);
    std::snprintf(buf, sizeof buf, "holes %d", holes); // the "eye": uncovered regions this frame
    ui.text(px + 12, ty, buf, 0.8f,
            holes > 0 ? std::array<float, 4>{1.0f, 0.35f, 0.35f, 1.0f} : tc);
    ty += ui.lineHeight(0.8f) + 2;
    if (perf1) line(perf1); // worst frame in the ring: phase breakdown
    if (perf2) line(perf2); // freshest input->present age
    ty += 6;
    auto legend = [&](std::array<float, 4> col, const char *lbl) {
        ui.fillRect(px + 12, ty + 2, 12, 12, col);
        ui.text(px + 30, ty, lbl, 0.78f, tc);
        ty += 19;
    };
    legend({0.25f, 0.9f, 0.45f, 1}, "done (converged)");
    legend({0.98f, 0.82f, 0.2f, 1}, "refining (coarse)");
    legend({1.0f, 0.5f, 0.15f, 1}, "queued");
}

// Numeric tick labels along the X and Y axes, matching the presenter's adaptive
// grid spacing. Labels ride the axis but clamp to the screen edge when the axis
// scrolls off, so coordinates stay readable while panning/zooming.
void drawAxisNumbers(Overlay &ui, const Viewport &vp, int fbW, int fbH)
{
    if (!ui.ok()) return;
    const double cx = vp.centerX, cy = vp.centerY, wpp = vp.worldPerPixel;
    if (!(wpp > 0.0)) return;
    const WorldRect wb = vp.worldBounds();
    // Same 1/2/5 x 10^n choice the presenter uses for the grid lines.
    const double rawStep = wpp * 90.0;
    const double mag = std::pow(10.0, std::floor(std::log10(std::max(rawStep, 1e-300))));
    const double norm = rawStep / mag;
    const double step = (norm < 2.0 ? 1.0 : norm < 5.0 ? 2.0 : 5.0) * mag;
    if (!(step > 0.0)) return;

    auto sx = [&](double wx) { return (wx - cx) / wpp + fbW * 0.5; };
    auto syTop = [&](double wy) { return fbH * 0.5 - (wy - cy) / wpp; };

    ui.begin();
    const float sc = 0.82f;
    const float lh = ui.lineHeight(sc);

    auto fmtStep = [&](double v, int decimals) -> std::string {
        char buf[32];
        if (std::abs(v) >= 1e5 || (v != 0.0 && std::abs(v) < 1e-3))
            std::snprintf(buf, sizeof buf, "%g", v);
        else
            std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
        return std::string(buf);
    };
    auto decimalsFor = [](double s) {
        return std::clamp(static_cast<int>(std::ceil(-std::log10(s))), 0, 8);
    };

    // LABEL step: the grid step widened along the 1/2/5 ladder until the widest
    // label on screen fits its spacing with margin -- labels can never collide,
    // however dense the grid is. (The grid itself stays at `step`.)
    const double maxAbsX = std::max(std::abs(wb.x0), std::abs(wb.x1));
    double lstep = step;
    for (int guard = 0; guard < 24; ++guard)
    {
        const int d = decimalsFor(lstep);
        const float widest =
            std::max(ui.textWidth(fmtStep(-maxAbsX, d), sc), ui.textWidth(fmtStep(maxAbsX, d), sc));
        if (lstep / wpp >= widest + 18.0f) break;
        const double m = std::pow(10.0, std::floor(std::log10(lstep * 1.0000001)));
        const double n = lstep / m;
        lstep = (n < 1.5 ? 2.0 : n < 3.0 ? 5.0 : 10.0) * m;
    }
    const int decimals = decimalsFor(lstep);
    auto fmt = [&](double v) { return fmtStep(v, decimals); };
    const std::array<float, 4> fg{0.95f, 0.97f, 1.0f, 1.0f};
    const std::array<float, 4> bg{0.03f, 0.03f, 0.05f, 0.9f};
    // Draw each label with a 1px dark outline so it stays legible over both the
    // bright (true) fill and the dark (false) background it straddles on the axis.
    auto label = [&](float x, float y, const std::string &s) {
        ui.text(x - 1.0f, y, s, sc, bg);
        ui.text(x + 1.0f, y, s, sc, bg);
        ui.text(x, y - 1.0f, s, sc, bg);
        ui.text(x, y + 1.0f, s, sc, bg);
        ui.text(x, y, s, sc, fg);
    };

    // X labels: just below the x-axis row, clamped clear of the top/bottom UI
    // bars. Integer-indexed positions (i * lstep) so no float drift accumulates.
    const float rowY = static_cast<float>(
        std::clamp(syTop(0.0) + 4.0, 44.0, static_cast<double>(fbH) - 32.0 - lh));
    {
        const long long i0 = static_cast<long long>(std::ceil(wb.x0 / lstep));
        const long long i1 = static_cast<long long>(std::floor(wb.x1 / lstep));
        for (long long i = i0; i <= i1 && i - i0 < 200; ++i)
        {
            if (i == 0) continue; // origin labelled once below
            const double x = static_cast<double>(i) * lstep;
            const float px = static_cast<float>(sx(x));
            if (px < 16 || px > fbW - 6) continue;
            label(px + 3, rowY, fmt(x));
        }
    }

    // Y labels: right-aligned just left of the y-axis column, clamped to the screen.
    {
        const long long j0 = static_cast<long long>(std::ceil(wb.y0 / lstep));
        const long long j1 = static_cast<long long>(std::floor(wb.y1 / lstep));
        for (long long j = j0; j <= j1 && j - j0 < 200; ++j)
        {
            if (j == 0) continue;
            const double y = static_cast<double>(j) * lstep;
            const float py = static_cast<float>(syTop(y));
            if (py < 44 || py > fbH - 30) continue;
            const std::string s = fmt(y);
            const float w = ui.textWidth(s, sc);
            const float colX = std::clamp(static_cast<float>(sx(0.0)) - w - 6.0f, 3.0f,
                                          static_cast<float>(fbW) - w - 3.0f);
            label(colX, py - lh * 0.5f, s);
        }
    }

    // origin
    const float ox = static_cast<float>(sx(0.0)), oy = static_cast<float>(syTop(0.0));
    if (ox > 10 && ox < fbW - 6 && oy > 44 && oy < fbH - 30) label(ox + 4, oy + 4, "0");
}
}

// Render the live GL pipeline headlessly to a PNG (validates context, shaders,
// tile-texture upload and compositing end-to-end). Usage: --selftest out.png [formula]
int runSelftest(const std::string &outPng, const std::string &formula, bool debug);
// Drive the REAL render loop offscreen through small -> maximize -> zoom-out and
// save a framebuffer readback after each settles. Validates the resize + texture
// upload path (the GL side of the maximize-then-zoom-out bug). Usage: --reprogl prefix
int runReproGl(const std::string &prefix);

int main(int argc, char **argv)
{
    if (argc >= 2 && std::string(argv[1]) == "--selftest")
    {
        const std::string out = argc >= 3 ? argv[2] : "selftest.png";
        const std::string f = argc >= 4 ? argv[3] : kPresets[0];
        const bool dbg = argc >= 5 && std::string(argv[4]) == "debug";
        return runSelftest(out, f, dbg);
    }
    if (argc >= 2 && std::string(argv[1]) == "--reprogl")
    {
        return runReproGl(argc >= 3 ? argv[2] : "reprogl_");
    }

    auto glfwLib = glfw::init();

    glfw::WindowHints{
        .clientApi = glfw::ClientApi::OpenGl,
        .contextVersionMajor = 3,
        .contextVersionMinor = 3,
        .openglProfile = glfw::OpenGlProfile::Core,
    }
        .apply();

    glfw::Window window(1000, 800, "GraphXplorer");
    glfw::makeContextCurrent(window);
    glfw::swapInterval(1); // vsync

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfw::getProcAddress)))
    {
        std::fprintf(stderr, "failed to load GL\n");
        return 1;
    }

    constexpr int tilePx = 64;
    // Wake the (possibly blocked) main thread when a worker publishes a tile.
    Engine engine(tilePx, 0, [] { glfw::postEmptyEvent(); });
    GlPresenter presenter(tilePx);
    Overlay overlay(findFont(), 22);

    auto [fbW, fbH] = window.getFramebufferSize();
    presenter.resize(fbW, fbH);
    overlay.resize(fbW, fbH);

    Viewport vp{0.0, 0.0, 16.0 / fbW, fbW, fbH}; // initial span ~[-8,8]
    std::string formula = argc >= 2 ? argv[1] : kPresets[0];

    auto rel = parseOrNull(formula);
    if (!rel) rel = parseOrNull(kPresets[0]);
    engine.setRelation(rel);
    engine.setViewport(vp);
    std::printf("GraphXplorer: %s\n", formula.c_str());

    bool dragging = false;
    double lastX = 0, lastY = 0;
    bool viewportDirty = false;

    // formula editing state
    bool editing = false;
    std::string editBuffer;
    size_t editPos = 0; // caret index into editBuffer
    std::string status;
    bool showDebug = false;

    // Diagnostics live under <source root>/out regardless of the working
    // directory the exe was launched from (a relative path silently wrote
    // nothing when started from build-release/engine or Explorer).
#ifdef GXR_ASSET_DIR
    const std::string outDir = std::string(GXR_ASSET_DIR) + "/out";
#else
    const std::string outDir = "out";
#endif
    std::error_code outDirEc;
    std::filesystem::create_directories(outDir, outDirEc);

    // Lightweight status log: resize/zoom, settle, store/job counts, GL errors.
    // NEVER write to the console or flush synchronously from the frame path: a
    // Windows console write can block the calling thread for hundreds of ms
    // (measured: two ~280ms mid-zoom freezes traced to the throttled [gen]
    // line's puts). File-only, OS-buffered; flush only on rare/final events.
    std::ofstream logf(outDir + "/gx.log", std::ios::trunc);
    auto logln = [&](const std::string &s, bool flush = false) {
        if (logf)
        {
            logf << s << "\n";
            if (flush) logf.flush();
        }
    };
    {
        char b[200];
        std::snprintf(b, sizeof b, "[start] fb=%dx%d wpp=%.5g level=%d", fbW, fbH, vp.worldPerPixel,
                      vp.activeLevel());
        logln(b);
    }

    // ---- Single shared render path --------------------------------------------
    // Defined BEFORE the window callbacks so the framebuffer-size and (crucially)
    // the window-REFRESH callbacks can drive it. On Windows a move/resize/maximize
    // runs a nested modal loop that blocks glfw event processing; the documented
    // GLFW remedy is to redraw from the window-refresh callback. The frame ends in
    // swapBuffers() THEN glFinish() so the result is actually made visible during
    // that blocking operation (otherwise the present is deferred and the resize
    // looks frozen/laggy). See glfwPollEvents docs + glfwSwapInterval(1) above.
    std::vector<PresentTile> present;
    std::vector<DebugTile> dbgTiles;
    auto lastT = std::chrono::steady_clock::now();
    auto lastDiag = lastT;
    double fps = 0.0, frameMs = 0.0;
    bool finalRender = false;
    bool prevFinal = false;
    bool rendering = false; // re-entry guard: a callback must not recurse into render

    // ---- Per-iteration latency attribution (out/gx_frames.csv + debug HUD) ------
    // Every frame logs where its time went, so a laggy session can be analyzed
    // after the fact: wait = blocked in waitEvents (idle/vsync pacing, NOT lag;
    // -1 = frame driven by a window callback), build = engine.buildPresent walk,
    // render = presenter total (upMs/upN = the glTexImage2D share), overlay =
    // text/axes, swap = fence+swapBuffers (vsync + driver), total = work without
    // wait. inAge = freshest-input-to-present age on frames that consumed a
    // viewport change (the latency the user feels while dragging/zooming).
    using SClock = std::chrono::steady_clock;
    const auto appT0 = SClock::now();
    auto msSince = [](SClock::time_point a) {
        return std::chrono::duration<double, std::milli>(SClock::now() - a).count();
    };
    struct FrameStat
    {
        double t{0}, wait{0}, build{0}, render{0}, up{0}, overlay{0}, swap{0}, total{0}, inAge{-1};
        int upN{0}, drawn{0}, holes{0}, inflight{0};
    };
    std::array<FrameStat, 240> perfRing{};
    size_t perfIdx = 0, perfCount = 0;
    double pendingWaitMs = -1.0; // set by the event loop, consumed by renderOnce
    SClock::time_point lastInputT{};
    bool haveInput = false;
    auto markInput = [&] {
        lastInputT = SClock::now();
        haveInput = true;
    };
    std::ofstream perfCsv(outDir + "/gx_frames.csv", std::ios::trunc);
    if (perfCsv)
        perfCsv << "t_ms,wait_ms,build_ms,render_ms,upload_ms,upload_n,overlay_ms,swap_ms,total_ms,"
                   "inAge_ms,present,drawn,holes,inflight,store\n";
    else
        std::fprintf(stderr, "perf csv: failed to open %s/gx_frames.csv\n", outDir.c_str());
    int perfFlush = 0;
    std::printf("perf log: %s/gx_frames.csv\n", outDir.c_str());

    // ---- Resize present guard (ported from the legacy app's complete fix) -------
    // The first present(s) at a new (larger) framebuffer size can trigger a driver
    // swapchain reallocation costing hundreds of ms to ~2s. An unconditional
    // glFinish turns that into a hard UI freeze; instead, for a short settle window
    // after each resize we wait on a GPU FENCE with a 10ms timeout -- if the GPU
    // isn't done we DON'T block, we skip this present and let the loop re-check next
    // frame, so the window stays responsive through the realloc. glFinish is only a
    // last-resort fallback. Zero cost in steady state (guard inactive).
    using Ms = std::chrono::milliseconds;
    constexpr Ms kGuardSettle{250};       // keep guarding for this long after a resize
    constexpr Ms kGuardMaxDuration{1000}; // hard cap on the whole guarded window
    constexpr int kGuardRequiredPresents = 3;
    constexpr int kGuardTimeoutsBeforeFinish = 3;
    constexpr uint64_t kFenceTimeoutNs = 10'000'000ULL; // 10ms per fence wait
    bool guardOn = false;
    std::chrono::steady_clock::time_point guardStarted{}, guardUntil{};
    int guardPresents = 0, guardTimeouts = 0;
    auto markGuard = [&]() {
        const auto now = std::chrono::steady_clock::now();
        guardOn = true;
        guardStarted = now;
        guardUntil = now + kGuardSettle;
        guardPresents = 0;
        guardTimeouts = 0;
    };
    auto guardActive = [&](std::chrono::steady_clock::time_point now) {
        if (!guardOn) return false;
        if (now >= guardUntil &&
            std::chrono::duration_cast<Ms>(now - guardStarted) >= kGuardMaxDuration)
            return false;
        return now < guardUntil || guardPresents < kGuardRequiredPresents;
    };

    auto renderOnce = [&]() {
        if (rendering) return; // a refresh fired while already rendering -> ignore
        rendering = true;

        FrameStat st;
        const auto fT0 = SClock::now();
        st.t = std::chrono::duration<double, std::milli>(fT0 - appT0).count();
        st.wait = pendingWaitMs;
        pendingWaitMs = -1.0; // callback-driven frames log wait = -1
        const bool vpChangedThisFrame = viewportDirty;

        if (viewportDirty)
        {
            engine.setViewport(vp);
            viewportDirty = false;
            finalRender = false;
        }

        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - lastT).count();
        lastT = now;
        if (dt > 0.0)
        {
            fps = fps * 0.9 + (1.0 / dt) * 0.1;
            frameMs = frameMs * 0.9 + dt * 1000.0 * 0.1;
        }

        const auto tBuild0 = SClock::now();
        const size_t visible = engine.buildPresent(vp, present);
        st.build = msSince(tBuild0);
        finalRender = (present.size() == visible);
        for (const PresentTile &p : present)
            if (p.fallback || p.state != TileState::Done)
            {
                finalRender = false;
                break;
            }

        const auto tRender0 = SClock::now();
        const int pendingUploads = presenter.renderFrame(vp, present, /*uploadBudget=*/192);
        st.render = msSince(tRender0);
        st.up = presenter.lastUploadMs();
        st.upN = presenter.lastUploads();
        st.drawn = presenter.lastDrawnTiles();
        // The "eye": true visual holes this frame -- a region that drew NOTHING (own
        // texture not ready AND no resident ancestor stand-in). Seamless swap => 0.
        const int holes = presenter.lastHoleTiles();
        st.holes = holes;
        if (pendingUploads > 0) finalRender = false;
        if (presenter.activeFades() > 0) finalRender = false; // crossfades still animating
        const auto tOverlay0 = SClock::now();
        drawAxisNumbers(overlay, vp, fbW, fbH);
        drawUi(overlay, fbW, fbH, formula, editing, editBuffer, editPos, status);
        if (showDebug)
        {
            engine.debugTiles(vp, dbgTiles);
            // HUD shows the worst frame of the ring (~2s) and the latest input age,
            // from COMPLETED frames (this frame's stats aren't final yet).
            const FrameStat *worst = nullptr;
            double inLast = -1;
            for (size_t i = 0; i < perfCount; ++i)
            {
                const FrameStat &f = perfRing[i];
                if (!worst || f.total > worst->total) worst = &f;
                if (f.inAge >= 0) inLast = f.inAge;
            }
            char p1[160], p2[160];
            if (worst)
                std::snprintf(p1, sizeof p1,
                              "worst2s %.1fms: bp %.1f rf %.1f (up %.1f/%d) sw %.1f wt %.1f",
                              worst->total, worst->build, worst->render, worst->up, worst->upN,
                              worst->swap, std::max(0.0, worst->wait));
            else
                std::snprintf(p1, sizeof p1, "worst2s: n/a");
            std::snprintf(p2, sizeof p2, "input->present %.1f ms", inLast);
            drawDebug(overlay, vp, fbW, fbH, dbgTiles, engine.storeSize(), engine.jobsCompleted(), fps,
                      frameMs, holes, p1, p2);
        }
        st.overlay = msSince(tOverlay0);

        // Resize-guarded present (see guard state above). In the settle window
        // after a resize, wait on a GPU fence with a bounded timeout for the draw
        // to complete before swapping; on timeout DON'T block -- skip this present
        // and retry next frame, so the UI stays live through the driver's swapchain
        // realloc. glFinish is only a last-resort fallback. Outside the guard the
        // fence is skipped entirely (zero steady-state cost).
        const auto tSwapPhase0 = SClock::now();
        bool doSwap = true;
        if (guardActive(now))
        {
            GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            if (fence)
            {
                const GLenum r = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, kFenceTimeoutNs);
                glDeleteSync(fence);
                if (r == GL_TIMEOUT_EXPIRED)
                {
                    const bool giveUp =
                        ++guardTimeouts >= kGuardTimeoutsBeforeFinish ||
                        std::chrono::duration_cast<Ms>(std::chrono::steady_clock::now() - guardStarted) >=
                            kGuardMaxDuration;
                    if (giveUp)
                    {
                        glFinish(); // last resort: accept one block rather than starve
                        guardTimeouts = 0;
                    }
                    else
                    {
                        doSwap = false; // GPU still busy (realloc) -> retry next frame
                    }
                }
                else
                {
                    guardTimeouts = 0;
                }
            }
            else
            {
                glFinish(); // no fence-sync support -> fallback
            }
        }
        if (doSwap)
        {
            const auto tSwap0 = std::chrono::steady_clock::now();
            window.swapBuffers();
            const double swapMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tSwap0)
                    .count();
            if (guardOn)
            {
                if (swapMs >= 50.0)
                {
                    // a slow present means the realloc isn't done -> extend the guard
                    guardUntil = std::chrono::steady_clock::now() + kGuardSettle;
                    guardPresents = 0;
                    guardTimeouts = 0;
                }
                else
                {
                    ++guardPresents;
                    if (!guardActive(std::chrono::steady_clock::now())) guardOn = false;
                }
            }
        }
        st.swap = msSince(tSwapPhase0); // fence wait + swapBuffers (vsync + driver)
        st.total = msSince(fT0);
        st.inflight = engine.jobsInFlight();
        if (vpChangedThisFrame && haveInput)
            st.inAge = std::chrono::duration<double, std::milli>(SClock::now() - lastInputT).count();
        perfRing[perfIdx] = st;
        perfIdx = (perfIdx + 1) % perfRing.size();
        perfCount = std::min(perfCount + 1, perfRing.size());
        if (perfCsv)
        {
            char row[280];
            std::snprintf(row, sizeof row,
                          "%.1f,%.2f,%.2f,%.2f,%.2f,%d,%.2f,%.2f,%.2f,%.2f,%zu,%d,%d,%d,%zu\n", st.t,
                          st.wait, st.build, st.render, st.up, st.upN, st.overlay, st.swap, st.total,
                          st.inAge, present.size(), st.drawn, st.holes, st.inflight,
                          engine.storeSize());
            perfCsv << row;
            if (++perfFlush >= 120)
            {
                perfCsv.flush();
                perfFlush = 0;
            }
        }

        // Quiet diagnostics: a GL error, ANY hole frame (the eye), the settle line,
        // or a throttled progress line while still generating tiles.
        const GLenum glErr = glGetError();
        const bool settled = finalRender && !prevFinal;
        const double sinceDiag = std::chrono::duration<double>(now - lastDiag).count();
        if (glErr != GL_NO_ERROR || holes > 0 || settled || (!finalRender && sinceDiag > 0.2))
        {
            int fbCount = 0;
            for (const PresentTile &p : present)
                if (p.fallback) ++fbCount;
            char b[240];
            std::snprintf(
                b, sizeof b,
                "[%s] fb=%dx%d wpp=%.5g present=%zu HOLES=%d fallback=%d store=%zu jobs=%llu glErr=0x%x",
                finalRender ? "final" : "gen", fbW, fbH, vp.worldPerPixel, present.size(), holes,
                fbCount, engine.storeSize(), static_cast<unsigned long long>(engine.jobsCompleted()),
                static_cast<unsigned>(glErr));
            // flush only on rare, diagnosis-critical lines; the throttled [gen]
            // progress lines stay buffered (they were the mid-zoom stall).
            logln(b, /*flush=*/glErr != GL_NO_ERROR || settled || holes > 0);
            lastDiag = now;
        }
        prevFinal = finalRender;

        rendering = false;
    };

    window.framebufferSizeEvent.setCallback([&](glfw::Window &, int w, int h) {
        fbW = std::max(1, w);
        fbH = std::max(1, h);
        presenter.resize(fbW, fbH); // stores size; glViewport is set each renderFrame
        overlay.resize(fbW, fbH);
        vp.pxW = fbW;
        vp.pxH = fbH;
        viewportDirty = true;
        markGuard(); // arm the resize present guard for the settle window
        char b[120];
        std::snprintf(b, sizeof b, "[resize] fb=%dx%d", fbW, fbH);
        logln(b);
        renderOnce(); // redraw the new size immediately, mid-resize
    });

    // THE fix for the "window waits then snaps" lag: GLFW calls this on each
    // WM_PAINT, including *inside* the blocking maximize/resize modal loop, so we
    // redraw (and the resize present guard keeps it responsive) while the OS owns
    // the main thread. Documented under glfwPollEvents.
    window.refreshEvent.setCallback([&](glfw::Window &) { renderOnce(); });

    // typed characters feed the formula editor (inserted at the caret)
    window.charEvent.setCallback([&](glfw::Window &, unsigned int codepoint) {
        if (editing && codepoint >= 32 && codepoint < 127)
        {
            editPos = std::min(editPos, editBuffer.size());
            editBuffer.insert(editBuffer.begin() + static_cast<ptrdiff_t>(editPos),
                              static_cast<char>(codepoint));
            ++editPos;
        }
    });

    window.mouseButtonEvent.setCallback(
        [&](glfw::Window &w, glfw::MouseButton b, glfw::MouseButtonState s, glfw::ModifierKeyBit) {
            if (b == glfw::MouseButton::Left)
            {
                dragging = (s == glfw::MouseButtonState::Press);
                auto [cx, cy] = w.getCursorPos();
                lastX = cx;
                lastY = cy;
            }
        });

    window.cursorPosEvent.setCallback([&](glfw::Window &, double cx, double cy) {
        if (!dragging) return;
        const double dx = cx - lastX, dy = cy - lastY;
        lastX = cx;
        lastY = cy;
        vp.centerX -= dx * vp.worldPerPixel;
        vp.centerY += dy * vp.worldPerPixel; // cursor y is down, world y is up
        viewportDirty = true;
        markInput();
    });

    window.scrollEvent.setCallback([&](glfw::Window &w, double, double yoff) {
        auto [cx, cy] = w.getCursorPos();
        const double worldX = vp.centerX + (cx - fbW * 0.5) * vp.worldPerPixel;
        const double worldY = vp.centerY - (cy - fbH * 0.5) * vp.worldPerPixel;
        const double factor = std::pow(1.1, -yoff);
        vp.worldPerPixel *= factor;
        vp.centerX = worldX - (cx - fbW * 0.5) * vp.worldPerPixel;
        vp.centerY = worldY + (cy - fbH * 0.5) * vp.worldPerPixel;
        viewportDirty = true;
        markInput();
    });

    window.keyEvent.setCallback(
        [&](glfw::Window &w, glfw::KeyCode key, int, glfw::KeyState action, glfw::ModifierKeyBit) {
            using K = glfw::KeyCode;
            const bool press = action == glfw::KeyState::Press;
            const bool held = press || action == glfw::KeyState::Repeat;

            if (editing)
            {
                editPos = std::min(editPos, editBuffer.size());
                if (key == K::Backspace && held && editPos > 0)
                {
                    editBuffer.erase(editPos - 1, 1);
                    --editPos;
                }
                if (key == K::Delete && held && editPos < editBuffer.size())
                    editBuffer.erase(editPos, 1);
                if (key == K::Left && held && editPos > 0) --editPos;
                if (key == K::Right && held && editPos < editBuffer.size()) ++editPos;
                if (key == K::Home && held) editPos = 0;
                if (key == K::End && held) editPos = editBuffer.size();
                if (!press) return;
                if (key == K::Enter || key == K::KeyPadEnter)
                {
                    std::string err;
                    auto parsed = Relation::parse(editBuffer, err);
                    if (parsed)
                    {
                        formula = editBuffer;
                        engine.setRelation(std::make_shared<const Relation>(std::move(*parsed)));
                        editing = false;
                        status.clear();
                        std::printf("GraphXplorer: %s\n", formula.c_str());
                    }
                    else
                    {
                        status = "parse error: " + err;
                    }
                }
                else if (key == K::Escape)
                {
                    editing = false;
                    status.clear();
                }
                return;
            }

            if (!press) return;
            if (key == K::Escape)
            {
                w.setShouldClose(true);
                return;
            }
            if (key == K::Enter || key == K::KeyPadEnter)
            {
                editing = true;
                editBuffer = formula;
                editPos = editBuffer.size();
                status.clear();
                return;
            }
            if (key == K::R)
            {
                vp.centerX = vp.centerY = 0.0;
                vp.worldPerPixel = 16.0 / fbW;
                viewportDirty = true;
                markInput();
                return;
            }
            if (key == K::D)
            {
                showDebug = !showDebug;
                return;
            }
            int idx = -1;
            if (key == K::One) idx = 0;
            else if (key == K::Two) idx = 1;
            else if (key == K::Three) idx = 2;
            else if (key == K::Four) idx = 3;
            else if (key == K::Five) idx = 4;
            else if (key == K::Six) idx = 5;
            if (idx >= 0)
            {
                formula = kPresets[idx];
                if (auto r = parseOrNull(formula))
                {
                    engine.setRelation(r);
                    status.clear();
                    std::printf("GraphXplorer: %s\n", formula.c_str());
                }
            }
        });

    while (!window.shouldClose())
    {
        // Event-driven: when the visible image is final, block at ~0% CPU until
        // input arrives or a worker wakes us (postEmptyEvent). While tiles are
        // still arriving, wake on each publish, with a short timeout as a backstop.
        const auto w0 = SClock::now();
        if (guardActive(std::chrono::steady_clock::now()))
            glfw::waitEvents(0.004); // settle window: keep re-checking the fence/realloc
        else if (finalRender)
            glfw::waitEvents();
        else if (presenter.activeFades() > 0)
            glfw::waitEvents(0.008); // crossfades animate at display cadence
        else
            glfw::waitEvents(0.05);
        pendingWaitMs = msSince(w0); // includes event-callback dispatch time
        renderOnce();
    }
    return 0;
}

int runSelftest(const std::string &outPng, const std::string &formula, bool debug)
{
    auto glfwLib = glfw::init();
    glfw::WindowHints{
        .clientApi = glfw::ClientApi::OpenGl,
        .contextVersionMajor = 3,
        .contextVersionMinor = 3,
        .openglProfile = glfw::OpenGlProfile::Core,
    }
        .apply();
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // offscreen

    const int W = 512, H = 512;
    glfw::Window window(W, H, "selftest");
    glfw::makeContextCurrent(window);
    glfw::swapInterval(0);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfw::getProcAddress)))
    {
        std::fprintf(stderr, "selftest: failed to load GL\n");
        return 1;
    }

    constexpr int tilePx = 64;
    Engine engine(tilePx);
    GlPresenter presenter(tilePx);
    Overlay overlay(findFont(), 22);
    auto [fbW, fbH] = window.getFramebufferSize();
    presenter.resize(fbW, fbH);
    overlay.resize(fbW, fbH);
    Viewport vp{0.0, 0.0, 16.0 / fbW, fbW, fbH};

    auto rel = parseOrNull(formula);
    if (!rel) return 1;
    engine.setRelation(rel);
    engine.setViewport(vp);

    std::vector<PresentTile> present;
    std::vector<DebugTile> dbgTiles;
    for (int f = 0; f < 200; ++f) // let workers solve coarse->fine
    {
        glfw::pollEvents();
        engine.buildPresent(vp, present);
        (void)presenter.renderFrame(vp, present, /*uploadBudget=*/64);
        drawUi(overlay, fbW, fbH, formula, /*editing=*/false, "", 0, "");
        if (debug)
        {
            engine.debugTiles(vp, dbgTiles);
            drawDebug(overlay, vp, fbW, fbH, dbgTiles, engine.storeSize(), engine.jobsCompleted(),
                      120.0, 8.0, /*holes=*/0);
        }
        window.swapBuffers();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::vector<uint8_t> px(static_cast<size_t>(fbW) * fbH * 4);
    glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    std::vector<uint8_t> flipped(px.size()); // GL is bottom-up; PNG is top-down
    for (int y = 0; y < fbH; ++y)
        std::memcpy(&flipped[static_cast<size_t>(fbH - 1 - y) * fbW * 4],
                    &px[static_cast<size_t>(y) * fbW * 4], static_cast<size_t>(fbW) * 4);
    if (!writePng(outPng, fbW, fbH, flipped))
    {
        std::fprintf(stderr, "selftest: failed to write %s\n", outPng.c_str());
        return 1;
    }
    std::printf("selftest: wrote %s (%dx%d)\n", outPng.c_str(), fbW, fbH);
    return 0;
}

int runReproGl(const std::string &prefix)
{
    auto glfwLib = glfw::init();
    glfw::WindowHints{
        .clientApi = glfw::ClientApi::OpenGl,
        .contextVersionMajor = 3,
        .contextVersionMinor = 3,
        .openglProfile = glfw::OpenGlProfile::Core,
    }
        .apply();
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    glfw::Window window(560, 360, "reprogl");
    glfw::makeContextCurrent(window);
    glfw::swapInterval(0);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfw::getProcAddress)))
    {
        std::fprintf(stderr, "reprogl: failed to load GL\n");
        return 1;
    }

    constexpr int tilePx = 64;
    Engine engine(tilePx, 0, [] { glfw::postEmptyEvent(); });
    GlPresenter presenter(tilePx);
    auto rel = parseOrNull("y > sin(2^x)");
    if (!rel) return 1;
    engine.setRelation(rel);

    auto sz0 = window.getFramebufferSize();
    int fbW = std::get<0>(sz0), fbH = std::get<1>(sz0);
    presenter.resize(fbW, fbH);
    Viewport vp{0.0, 0.0, 8.0 / fbW, fbW, fbH};

    std::vector<PresentTile> present;
    // Drive the REAL loop (buildPresent -> renderFrame with the pending-upload
    // guard) until the image is fully solved AND uploaded, then read it back.
    auto renderToCompletion = [&](const std::string &name) {
        engine.setViewport(vp);
        int frame = 0;
        bool finalRender = false;
        double maxBp = 0, maxRf = 0, sumBp = 0;
        int maxHoles = 0, holeFrames = 0;
        for (; frame < 1500; ++frame)
        {
            glfw::pollEvents();
            const auto t0 = std::chrono::steady_clock::now();
            const size_t visible = engine.buildPresent(vp, present);
            const auto t1 = std::chrono::steady_clock::now();
            finalRender = (present.size() == visible);
            int holes = 0;
            for (const PresentTile &p : present)
            {
                if (!p.flat && !p.cov) ++holes;
                if (p.fallback || p.state != TileState::Done) finalRender = false;
            }
            maxHoles = std::max(maxHoles, holes);
            if (holes > 0) ++holeFrames;
            const int pending = presenter.renderFrame(vp, present, 128);
            const auto t2 = std::chrono::steady_clock::now();
            const double bp = std::chrono::duration<double, std::milli>(t1 - t0).count();
            maxBp = std::max(maxBp, bp);
            sumBp += bp;
            maxRf = std::max(maxRf, std::chrono::duration<double, std::milli>(t2 - t1).count());
            if (pending > 0) finalRender = false;
            window.swapBuffers();
            if (finalRender && frame > 2) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        int fb = 0;
        for (const PresentTile &p : present)
            if (p.fallback) ++fb;
        saveFramebuffer(fbW, fbH, prefix + name + ".png");
        const bool frozen = (frame >= 1500 && fb > 0);
        std::printf("%-12s fb=%4dx%-4d wpp=%-8.4g frames=%-4d FB=%-3d HOLES(max=%d,frames=%d) "
                    "jobs=%-6llu store=%-5zu %s\n",
                    name.c_str(), fbW, fbH, vp.worldPerPixel, frame, fb, maxHoles, holeFrames,
                    static_cast<unsigned long long>(engine.jobsCompleted()), engine.storeSize(),
                    frozen ? "*** FROZEN ***" : "ok");
    };

    // Scrub like a live user: continuous small zoom/pan steps, a few buildPresents
    // per step (no full settle) -- this is what produces the transient single-frame
    // holes. Counts hole frames across the whole scrub. `guard` (optional) is a
    // world rect that was FULLY PAINTED before the scrub: a bare tile overlapping
    // it means previously-shown content got replaced by background (the
    // "black-flash" class of bug) -- that count must stay 0. Bare tiles outside
    // the guard are newly-revealed territory awaiting first paint (the accepted
    // immersion carve-out).
    auto scrub = [&](const std::string &name, int steps, double zoomMul, double panPx, int fps = 4,
                     const WorldRect *guard = nullptr) {
        int maxHoles = 0, holeFrames = 0, total = 0, maxPend = 0, pendFrames = 0;
        int guardBareFrames = 0, guardBareMax = 0;
        for (int s = 0; s < steps; ++s)
        {
            vp.worldPerPixel *= zoomMul;
            vp.centerX += panPx * vp.worldPerPixel;
            engine.setViewport(vp);
            glfw::pollEvents();
            for (int f = 0; f < fps; ++f, ++total)
            {
                const size_t visible = engine.buildPresent(vp, present);
                (void)visible;
                int holes = 0, guardBare = 0;
                for (const PresentTile &p : present)
                    if (!p.flat && !p.cov)
                    {
                        ++holes;
                        if (guard && !(p.rect.x1 < guard->x0 || p.rect.x0 > guard->x1 ||
                                       p.rect.y1 < guard->y0 || p.rect.y0 > guard->y1))
                            ++guardBare;
                    }
                maxHoles = std::max(maxHoles, holes);
                if (holes > 0) ++holeFrames;
                guardBareMax = std::max(guardBareMax, guardBare);
                if (guardBare > 0) ++guardBareFrames;
                // pend = tiles still sharpening in (own texture uploading; a stand-in
                // is drawn meanwhile -> NOT a hole). trueHoles = regions that drew
                // NOTHING. The seamless target is trueHoleFrames == 0.
                const int pend = presenter.renderFrame(vp, present, 192);
                const int trueHoles = presenter.lastHoleTiles();
                maxPend = std::max(maxPend, trueHoles);
                if (trueHoles > 0) ++pendFrames;
                (void)pend;
                window.swapBuffers();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        saveFramebuffer(fbW, fbH, prefix + name + ".png");
        std::printf("%-14s frames=%-4d covHoleFrames=%-3d TRUEHOLE(max=%d,frames=%d)%s store=%zu\n",
                    name.c_str(), total, holeFrames, maxPend, pendFrames,
                    guard ? (std::string("  GUARDBARE(max=") + std::to_string(guardBareMax) +
                             ",frames=" + std::to_string(guardBareFrames) + ")")
                                .c_str()
                          : "",
                    engine.storeSize());
    };

    auto setWindow = [&](int w, int h) {
        window.setSize(w, h);
        glfw::pollEvents();
        auto s = window.getFramebufferSize();
        fbW = std::get<0>(s);
        fbH = std::get<1>(s);
        presenter.resize(fbW, fbH);
        vp.pxW = fbW;
        vp.pxH = fbH;
    };

    auto probe = [&](const char *name) {
        int nonDone = 0, mixed = 0;
        for (const PresentTile &p : present)
        {
            if (p.flat) continue;
            ++mixed;
            if (p.state != TileState::Done) ++nonDone;
        }
        std::printf("%-16s mixedVisible=%-5d nonDone=%-4d store=%zu\n", name, mixed, nonDone,
                    engine.storeSize());
    };

    setWindow(3200, 1859);
    renderToCompletion("1small");

    // (A) Seamless swap: fast zoom across detail boundaries on cached tiles -> 0 holes.
    vp.centerX = 50.0;
    vp.centerY = 0.0;
    vp.worldPerPixel = 0.06;
    renderToCompletion("preload");
    scrub("warmIn", 30, 0.95, 0.0, 8);
    scrub("fastIn", 30, 0.95, 0.0, 1);
    scrub("fastOut", 30, 1.0 / 0.95, 0.0, 1);

    // (A2) FRESH zoom-out: scrub OUT well past every previously-visited level, so
    // each coarser detail level is born with never-classified ancestors. The
    // black-flash regression was exactly here: an Unknown root blocked the descent
    // to the already-painted subtree for 1-2 frames -> background over content the
    // user was just looking at. Guard = the painted view at scrub start: bare
    // tiles over it must be 0 (the reveal ring outside it may briefly show
    // background until first paint -- the carve-out).
    {
        const WorldRect painted = vp.worldBounds();
        scrub("freshOut", 45, 1.0 / 0.95, 0.0, 2, &painted);
    }

    // (B) Cascade continuation: a deep zoom AFTER prior activity must still converge
    // (all tiles Done) instead of dead-ending at an already-classified intermediate.
    vp.centerX = 15.0;
    vp.centerY = 0.3;
    vp.worldPerPixel = 0.012; // shallow first -> classifies ancestors
    renderToCompletion("B-medium");
    vp.worldPerPixel = 0.001; // then deep, SAME center
    renderToCompletion("B-deepSame");
    probe("B-deepSame");
    vp.centerX = -40.0; // deep at a NEW region after activity
    vp.centerY = 0.7;
    vp.worldPerPixel = 0.0008;
    renderToCompletion("B-deepNew");
    probe("B-deepNew");

    std::printf(
        "(want: no FROZEN, fast* TRUEHOLE frames=0, freshOut GUARDBARE frames=0, B-* nonDone=0)\n");
    return 0;
}
