// GraphXplorer2 - live CPU renderer. Pan = drag, zoom = scroll, 1-6 = preset
// formulas, R = reset view, Esc = quit. All GL on the main thread; all math async.

#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <glfwpp/glfwpp.h>

#include "app/Engine.h"
#include "image/Png.h"
#include "present/GlPresenter.h"
#include "present/Overlay.h"

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

// Draw the on-screen UI: a formula bar (editable), a status line, and a help bar.
void drawUi(Overlay &ui, int fbW, int fbH, const std::string &formula, bool editing,
           const std::string &edit, const std::string &status)
{
    if (!ui.ok()) return;
    ui.begin();
    const float w = static_cast<float>(fbW), h = static_cast<float>(fbH);

    // top formula bar
    ui.fillRect(0, 0, w, 40, {0.05f, 0.05f, 0.08f, 0.86f});
    const std::string shown = editing ? ("f:  " + edit + "_") : ("f:  " + formula);
    const std::array<float, 4> fcol = editing ? std::array<float, 4>{1.0f, 0.92f, 0.55f, 1.0f}
                                              : std::array<float, 4>{0.88f, 0.94f, 1.0f, 1.0f};
    ui.text(14, 9, shown, 1.0f, fcol);

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
// info panel (fps, level, viewport, tile/store/job counts) and a legend.
void drawDebug(Overlay &ui, const Viewport &vp, int fbW, int fbH,
              const std::vector<DebugTile> &tiles, size_t storeSize, unsigned long long jobs,
              double fps, double frameMs)
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
    const float pw = 320, ph = 184, px = fbW - pw - 10, py = 50;
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

    glfw::Window window(1000, 800, "GraphXplorer2");
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
    std::printf("GraphXplorer2: %s\n", formula.c_str());

    bool dragging = false;
    double lastX = 0, lastY = 0;
    bool viewportDirty = false;

    // formula editing state
    bool editing = false;
    std::string editBuffer;
    std::string status;
    bool showDebug = false;

    // Lightweight status log: resize/zoom, settle, store/job counts, GL errors.
    std::ofstream logf("out/gx2.log", std::ios::trunc);
    auto logln = [&](const std::string &s) {
        if (logf)
        {
            logf << s << "\n";
            logf.flush();
        }
        std::puts(s.c_str());
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

        const size_t visible = engine.buildPresent(vp, present);
        finalRender = (present.size() == visible);
        for (const PresentTile &p : present)
            if (p.fallback || p.state != TileState::Done)
            {
                finalRender = false;
                break;
            }

        const int pendingUploads = presenter.renderFrame(vp, present, /*uploadBudget=*/64);
        if (pendingUploads > 0) finalRender = false;
        drawUi(overlay, fbW, fbH, formula, editing, editBuffer, status);
        if (showDebug)
        {
            engine.debugTiles(vp, dbgTiles);
            drawDebug(overlay, vp, fbW, fbH, dbgTiles, engine.storeSize(), engine.jobsCompleted(), fps,
                      frameMs);
        }

        // Resize-guarded present (see guard state above). In the settle window
        // after a resize, wait on a GPU fence with a bounded timeout for the draw
        // to complete before swapping; on timeout DON'T block -- skip this present
        // and retry next frame, so the UI stays live through the driver's swapchain
        // realloc. glFinish is only a last-resort fallback. Outside the guard the
        // fence is skipped entirely (zero steady-state cost).
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

        // Quiet diagnostics: a GL error, the settle line, or a throttled progress
        // line while still generating tiles.
        const GLenum glErr = glGetError();
        const bool settled = finalRender && !prevFinal;
        const double sinceDiag = std::chrono::duration<double>(now - lastDiag).count();
        if (glErr != GL_NO_ERROR || settled || (!finalRender && sinceDiag > 0.2))
        {
            int fbCount = 0;
            for (const PresentTile &p : present)
                if (p.fallback) ++fbCount;
            char b[220];
            std::snprintf(b, sizeof b,
                          "[%s] fb=%dx%d wpp=%.5g present=%zu fallback=%d store=%zu jobs=%llu glErr=0x%x",
                          finalRender ? "final" : "gen", fbW, fbH, vp.worldPerPixel, present.size(),
                          fbCount, engine.storeSize(),
                          static_cast<unsigned long long>(engine.jobsCompleted()),
                          static_cast<unsigned>(glErr));
            logln(b);
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

    // typed characters feed the formula editor
    window.charEvent.setCallback([&](glfw::Window &, unsigned int codepoint) {
        if (editing && codepoint >= 32 && codepoint < 127)
        {
            editBuffer.push_back(static_cast<char>(codepoint));
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
    });

    window.keyEvent.setCallback(
        [&](glfw::Window &w, glfw::KeyCode key, int, glfw::KeyState action, glfw::ModifierKeyBit) {
            using K = glfw::KeyCode;
            const bool press = action == glfw::KeyState::Press;
            const bool held = press || action == glfw::KeyState::Repeat;

            if (editing)
            {
                if ((key == K::Backspace) && held && !editBuffer.empty()) editBuffer.pop_back();
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
                        std::printf("GraphXplorer2: %s\n", formula.c_str());
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
                status.clear();
                return;
            }
            if (key == K::R)
            {
                vp.centerX = vp.centerY = 0.0;
                vp.worldPerPixel = 16.0 / fbW;
                viewportDirty = true;
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
                    std::printf("GraphXplorer2: %s\n", formula.c_str());
                }
            }
        });

    while (!window.shouldClose())
    {
        // Event-driven: when the visible image is final, block at ~0% CPU until
        // input arrives or a worker wakes us (postEmptyEvent). While tiles are
        // still arriving, wake on each publish, with a short timeout as a backstop.
        if (guardActive(std::chrono::steady_clock::now()))
            glfw::waitEvents(0.004); // settle window: keep re-checking the fence/realloc
        else if (finalRender)
            glfw::waitEvents();
        else
            glfw::waitEvents(0.05);
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
        drawUi(overlay, fbW, fbH, formula, /*editing=*/false, "", "");
        if (debug)
        {
            engine.debugTiles(vp, dbgTiles);
            drawDebug(overlay, vp, fbW, fbH, dbgTiles, engine.storeSize(), engine.jobsCompleted(),
                      120.0, 8.0);
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
        for (; frame < 1500; ++frame)
        {
            glfw::pollEvents();
            const auto t0 = std::chrono::steady_clock::now();
            const size_t visible = engine.buildPresent(vp, present);
            const auto t1 = std::chrono::steady_clock::now();
            finalRender = (present.size() == visible);
            for (const PresentTile &p : present)
                if (p.fallback || p.state != TileState::Done)
                {
                    finalRender = false;
                    break;
                }
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
        std::printf("%-12s fb=%4dx%-4d wpp=%-8.4g frames=%-4d FB=%-3d jobs=%-6llu store=%-5zu "
                    "maxBpMs=%5.1f maxRfMs=%5.1f sumBpMs=%6.0f %s\n",
                    name.c_str(), fbW, fbH, vp.worldPerPixel, frame, fb,
                    static_cast<unsigned long long>(engine.jobsCompleted()), engine.storeSize(),
                    maxBp, maxRf, sumBp, frozen ? "*** FROZEN ***" : "ok");
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

    renderToCompletion("1small");

    // Repeated maximize cycles at the user's window size (3200x1859), with some
    // zooming in between to grow the store like real use. Watch MAXIMIZE* maxBpMs
    // / sumBpMs: if they grow across cycles, that growth IS the accumulating lag.
    for (int m = 0; m < 6; ++m)
    {
        setWindow(900, 600);
        vp.worldPerPixel *= 3.0;
        renderToCompletion("c" + std::to_string(m) + "_zoomout");
        vp.worldPerPixel /= 3.0;
        renderToCompletion("c" + std::to_string(m) + "_zoomin");
        setWindow(3200, 1859);
        renderToCompletion("MAXIMIZE" + std::to_string(m));
    }

    std::printf("(MAXIMIZE* maxBpMs/sumBpMs growth across cycles = the accumulating lag)\n");
    return 0;
}
