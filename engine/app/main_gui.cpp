// GraphXplorer - live CPU renderer. Pan = drag, zoom = scroll, 1-6 = preset
// formulas, R = reset view, Esc = quit. All GL on the main thread; all math async.

#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <glfwpp/glfwpp.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#endif

#include "app/Engine.h"
#include "image/Png.h"
#include "present/Glass.h"
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

// Showcase presets (keys 1-6): each one leans on a renderer capability at its
// limit. 1: the signature wall -- smooth curve degrading into the exact
// analytic-measure gray (explicit 1-D fast path). 2: 2-D oscillation
// checkerboard; zoom OUT and the moire converges to stable gray, zoom in and
// greedy proofs carve exact regions. 3: ripple rings densifying to gray with
// interval-proved cores. 4: an implicit curve WEB -- the marching-squares
// vector strokes at full stretch. 5: the topologist's sine curve -- infinitely
// accumulating oscillation at x=0, sound at every zoom forever. 6: the same
// wall as 1 but with the structure detection DEFEATED (+y*0): the full
// pathological 2-D path, the formula the responsiveness contract is measured
// against -- pan and zoom into the gray and it must never lag.
const char *kPresets[] = {
    "y > sin(2^x)",
    "sin(x*y) > 0",
    "sin(x^2 + y^2) > 0",
    "sin(x)*sin(y) = sin(x*y)",
    "y < sin(1/x)",
    "y > sin(2^x) + y*0",
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

// Floating-card UI. The formula list is a rounded panel in the top-left
// (one row per relation, slot-color accent bar, selected row tinted, inline
// caret + parse error); a slim hint pill sits bottom-center. Rounded corners
// are the classic two-rect notch trick -- no new Overlay primitives.
void roundedRect(Overlay &ui, float x, float y, float w, float h, float r,
                 const std::array<float, 4> &c)
{
    ui.fillRect(x + r, y, w - 2 * r, h, c);
    ui.fillRect(x, y + r, w, h - 2 * r, c);
}

void drawUi(Overlay &ui, Glass &glass, int fbW, int fbH, float s,
           const std::vector<std::string> &formulas, size_t selected, float selAnim,
           bool editing, const std::string &edit, size_t editPos, const std::string &status)
{
    if (!ui.ok()) return;
    const float w = static_cast<float>(fbW), h = static_cast<float>(fbH);
    const float rowH = 30.0f * s, pad = 10.0f * s;
    constexpr float textScale = 0.92f;

    // --- formula card (frosted glass) ---
    float cardW = 300.0f * s;
    for (size_t i = 0; i < formulas.size(); ++i)
    {
        const std::string &str = (editing && i == selected) ? edit : formulas[i];
        cardW = std::max(cardW, ui.textWidth(str, textScale) + 52.0f * s);
    }
    cardW = std::min(cardW, w - 24.0f * s);
    const bool showStatus = !status.empty();
    // clamp the card to the window; rows SCROLL so the selected one stays
    // visible (a tall list on a small hi-dpi window must not overflow)
    const float maxCardH = h - 100.0f * s;
    size_t maxRows = static_cast<size_t>(
        std::max(1.0f, (maxCardH - pad * 2 - (showStatus ? 20.0f * s : 0.0f)) / rowH));
    const size_t rows = std::min(formulas.size(), maxRows);
    size_t firstRow = 0;
    if (formulas.size() > rows && selected >= rows)
        firstRow = std::min(selected - rows + 1, formulas.size() - rows);
    const float cardH = pad * 2 + rowH * static_cast<float>(rows) +
                        (showStatus ? 20.0f * s : 0.0f);

    // hint pill geometry first, so both panels frost from one capture
    const float hintScale = 0.72f;
    std::string hint = editing
        ? "Enter apply    Esc cancel"
        : "Tab select    Enter edit    N new    X delete    1-6 presets    S settings    D debug";
    float tw = ui.textWidth(hint, hintScale);
    if (tw + 28.0f * s > w - 24.0f * s)
    {
        hint = editing ? "Enter ok   Esc" : "Tab   Enter   N   X   1-6   S   D";
        tw = ui.textWidth(hint, hintScale);
    }
    if (tw + 28.0f * s > w - 24.0f * s)
    {
        hint = editing ? "Enter / Esc" : "Tab  Enter  N  X";
        tw = ui.textWidth(hint, hintScale);
    }
    const float pillW = std::min(tw + 28.0f * s, w - 24.0f * s), pillH = 26.0f * s;
    const float px = (w - pillW) * 0.5f, py = h - 40.0f * s;

    glass.capture();
    glass.panel(12 * s, 12 * s, cardW, cardH, 12.0f * s);
    glass.panel(px, py, pillW, pillH, pillH * 0.5f);

    ui.begin();
    // selection highlight glides between rows (selAnim eases toward selected)
    if (!formulas.empty())
    {
        const float hy = 12 * s + pad + rowH * (selAnim - static_cast<float>(firstRow)) - 1;
        const bool ed = editing;
        if (hy > 12 * s && hy < 12 * s + cardH - rowH * 0.5f)
            roundedRect(ui, 18 * s, hy, cardW - 12 * s, rowH - 4 * s, 4 * s,
                        {1.0f, 1.0f, 1.0f, ed ? 0.10f : 0.06f});
    }
    for (size_t i = firstRow; i < firstRow + rows && i < formulas.size(); ++i)
    {
        const float y = 12 * s + pad + rowH * static_cast<float>(i - firstRow);
        const bool sel = i == selected;
        const bool ed = editing && sel;
        const float *pal = kRelationPalette[i & 7];
        ui.fillRect(22 * s, y + 4 * s, 3 * s, rowH - 14 * s,
                    {pal[0], pal[1], pal[2], sel ? 1.0f : 0.75f});
        const std::string &shown = ed ? edit : formulas[i];
        const std::array<float, 4> fcol = ed ? std::array<float, 4>{0.98f, 0.99f, 1.0f, 1.0f}
                                          : sel ? std::array<float, 4>{0.88f, 0.92f, 0.98f, 1.0f}
                                                : std::array<float, 4>{0.62f, 0.67f, 0.76f, 1.0f};
        ui.text(36 * s, y + 2 * s, shown, textScale, fcol);
        if (ed)
        {
            const std::string prefix = shown.substr(0, std::min(editPos, shown.size()));
            const float cx = 36.0f * s + ui.textWidth(prefix, textScale) + 1.0f;
            ui.fillRect(cx, y + 1 * s, 2 * s, 20 * s, {pal[0], pal[1], pal[2], 1.0f});
        }
    }
    if (firstRow > 0) ui.text(cardW - 8 * s, 14 * s, "^", 0.7f, {0.6f, 0.65f, 0.75f, 0.8f});
    if (firstRow + rows < formulas.size())
        ui.text(cardW - 8 * s, 12 * s + cardH - 16 * s, "v", 0.7f, {0.6f, 0.65f, 0.75f, 0.8f});
    if (showStatus)
        ui.text(24 * s, 12 * s + pad + rowH * static_cast<float>(rows) + 2 * s, status, 0.72f,
                {1.0f, 0.48f, 0.48f, 1.0f});

    ui.text(px + 14.0f * s, py + 5.0f * s, hint, hintScale, {0.62f, 0.67f, 0.77f, 1.0f});
}

// ---- Settings page: a frosted panel of widget rows (toggles + sliders),
// keyboard-driven: Up/Down select, Left/Right adjust, S or Esc closes. ----
struct AppSettings
{
    bool grid = true;
    bool labels = true;
    float fillOpacity = 0.55f;
    float uiScaleMul = 1.0f;
};

void drawSettings(Overlay &ui, Glass &glass, int fbW, int fbH, float s, const AppSettings &cfg,
                 int sel)
{
    if (!ui.ok()) return;
    const float w = static_cast<float>(fbW), h = static_cast<float>(fbH);
    const float pw = 380 * s, rowH = 40 * s, titleH = 46 * s;
    const float ph = titleH + rowH * 4 + 14 * s;
    const float x = (w - pw) * 0.5f, y = (h - ph) * 0.5f;
    glass.panel(x, y, pw, ph, 14 * s);

    ui.begin();
    ui.text(x + 20 * s, y + 12 * s, "Settings", 1.0f, {0.92f, 0.95f, 1.0f, 1.0f});

    const char *names[4] = {"Grid", "Axis labels", "Fill opacity", "UI scale"};
    for (int i = 0; i < 4; ++i)
    {
        const float ry = y + titleH + rowH * static_cast<float>(i);
        if (i == sel)
            roundedRect(ui, x + 8 * s, ry, pw - 16 * s, rowH - 6 * s, 5 * s,
                        {1.0f, 1.0f, 1.0f, 0.07f});
        const bool on = i == sel;
        ui.text(x + 20 * s, ry + 8 * s, names[i], 0.85f,
                on ? std::array<float, 4>{0.92f, 0.95f, 1.0f, 1.0f}
                   : std::array<float, 4>{0.62f, 0.67f, 0.76f, 1.0f});

        const float vx = x + pw - 150 * s;
        if (i <= 1) // toggle widget
        {
            const bool v = i == 0 ? cfg.grid : cfg.labels;
            const float tx = x + pw - 66 * s, ty = ry + 9 * s, twW = 44 * s, twH = 20 * s;
            roundedRect(ui, tx, ty, twW, twH, twH * 0.5f,
                        v ? std::array<float, 4>{0.00f, 0.45f, 0.85f, 0.9f}
                          : std::array<float, 4>{1.0f, 1.0f, 1.0f, 0.12f});
            const float knob = twH - 6 * s;
            roundedRect(ui, v ? tx + twW - knob - 3 * s : tx + 3 * s, ty + 3 * s, knob, knob,
                        knob * 0.5f, {0.95f, 0.97f, 1.0f, 1.0f});
        }
        else // slider widget
        {
            const float v01 = i == 2 ? (cfg.fillOpacity - 0.20f) / 0.80f
                                     : (cfg.uiScaleMul - 0.75f) / 0.75f;
            const float tx = vx, ty = ry + 17 * s, twW = 96 * s;
            ui.fillRect(tx, ty, twW, 4 * s, {1.0f, 1.0f, 1.0f, 0.14f});
            ui.fillRect(tx, ty, twW * std::clamp(v01, 0.0f, 1.0f), 4 * s,
                        {0.00f, 0.55f, 0.98f, 0.95f});
            const float kx = tx + twW * std::clamp(v01, 0.0f, 1.0f) - 4 * s;
            roundedRect(ui, kx, ty - 5 * s, 8 * s, 14 * s, 3 * s, {0.95f, 0.97f, 1.0f, 1.0f});
            char buf[16];
            std::snprintf(buf, sizeof buf, i == 2 ? "%.0f%%" : "%.2fx",
                          i == 2 ? cfg.fillOpacity * 100.0f : cfg.uiScaleMul);
            ui.text(x + pw - 46 * s, ry + 8 * s, buf, 0.72f, {0.62f, 0.67f, 0.76f, 1.0f});
        }
    }
}

// Debug overlay: visible tiles outlined and coloured by solve state, plus an
// info panel (fps, level, viewport, tile/store/job counts, frame-latency
// attribution) and a legend.
void drawDebug(Overlay &ui, Glass &glass, float s, const Viewport &vp, int fbW, int fbH,
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

    // info panel (top-right, frosted; clamped so small windows are not swallowed)
    const float pw = std::min(430 * s, fbW - 28.0f * s);
    const float ph = std::min((perf1 ? 296 : 206) * s, fbH - 60.0f * s);
    const float px = fbW - pw - 14 * s, py = 14 * s;
    glass.panel(px, py, pw, ph, 12.0f * s);
    ui.begin(); // glass switched programs; re-arm the overlay batch state
    const std::array<float, 4> tc{0.85f, 0.9f, 1.0f, 1.0f};
    float ty = py + 8 * s;
    char buf[160];
    auto line = [&](const char *str) {
        if (ty + ui.lineHeight(0.8f) > py + ph - 6 * s) return; // clip to the panel
        ui.text(px + 12 * s, ty, str, 0.8f, tc);
        ty += ui.lineHeight(0.8f) + 2 * s;
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
        if (ty + ui.lineHeight(0.8f) > py + ph - 6 * s) return;
        ui.fillRect(px + 12 * s, ty + 2 * s, 12 * s, 12 * s, col);
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
void drawAxisNumbers(Overlay &ui, const Viewport &vp, int fbW, int fbH, float barH,
                    float wake)
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
    const float sc = 0.74f;
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
    const float wk = 0.25f + 0.75f * wake; // labels sleep with the rest of the chrome
    const std::array<float, 4> fg{0.62f, 0.64f, 0.69f, 0.95f * wk};
    const std::array<float, 4> bg{0.03f, 0.03f, 0.05f, 0.75f * wk};
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
        std::clamp(syTop(0.0) + 4.0, static_cast<double>(barH) + 4.0,
                   static_cast<double>(fbH) - 32.0 - lh));
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
            if (py < barH + 4 || py > fbH - 30) continue;
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
float gSelftestScale = 0.0f; // 0 = use the window content scale
int runSelftest(const std::string &outPng, const std::string &formula, bool debug,
                int W, int H);
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
        bool dbg = false;
        int W = 512, H = 512;
        for (int i = 5; i <= argc; ++i) // optional: debug and/or WxH in either order
        {
            if (i - 1 >= 4 && argv[i - 1])
            {
                const std::string a = argv[i - 1];
                if (a == "debug") dbg = true;
                else if (const size_t xPos = a.find('x'); xPos != std::string::npos)
                {
                    W = std::max(64, std::atoi(a.substr(0, xPos).c_str()));
                    H = std::max(64, std::atoi(a.substr(xPos + 1).c_str()));
                    if (const size_t at = a.find('@'); at != std::string::npos)
                        gSelftestScale = static_cast<float>(std::atof(a.substr(at + 1).c_str()));
                }
            }
        }
        return runSelftest(out, f, dbg, W, H);
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
    Glass glass;
    // DPI-aware UI: bake the glyph atlas at the monitor content scale; every
    // UI metric multiplies by uiS() (content scale x the Settings multiplier).
    float dpiScale = std::get<0>(window.getContentScale());
    if (!(dpiScale >= 0.5f && dpiScale <= 4.0f)) dpiScale = 1.0f;
    Overlay overlay(findFont(), static_cast<int>(22.0f * dpiScale + 0.5f));
#ifdef _WIN32
    if (HWND hwnd = glfwGetWin32Window(static_cast<GLFWwindow *>(window)))
    {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark);
        int backdrop = 2;
        DwmSetWindowAttribute(hwnd, 38, &backdrop, sizeof backdrop);
    }
#endif

    auto [fbW, fbH] = window.getFramebufferSize();
    presenter.resize(fbW, fbH);
    glass.resize(fbW, fbH);
    overlay.resize(fbW, fbH);

    Viewport vp{0.0, 0.0, 16.0 / fbW, fbW, fbH}; // initial span ~[-8,8]
    std::vector<std::string> formulas{argc >= 2 ? argv[1] : kPresets[0]};
    auto rel0 = parseOrNull(formulas[0]);
    if (!rel0)
    {
        formulas[0] = kPresets[0];
        rel0 = parseOrNull(formulas[0]);
    }
    std::vector<std::shared_ptr<const Relation>> rels{rel0};
    size_t selected = 0;
    engine.setRelations(rels);
    engine.setViewport(vp);
    std::printf("GraphXplorer: %s\n", formulas[0].c_str());

    bool dragging = false;
    double lastX = 0, lastY = 0;
    double mouseX = 0, mouseY = 0; // latest cursor (screen px) for the readout
    bool wantScreenshot = false;
    bool viewportDirty = false;

    AppSettings cfg;
    bool settingsOpen = false;
    int settingsSel = 0;
    auto uiS = [&]() { return dpiScale * cfg.uiScaleMul; };
#ifdef GXR_ASSET_DIR
    const std::string cfgPath = std::string(GXR_ASSET_DIR) + "/out/gx_settings.ini";
#else
    const std::string cfgPath = "out/gx_settings.ini";
#endif
    {
        std::ifstream in(cfgPath);
        std::string k;
        double v;
        while (in >> k >> v)
        {
            if (k == "grid") cfg.grid = v != 0;
            else if (k == "labels") cfg.labels = v != 0;
            else if (k == "fillOpacity")
                cfg.fillOpacity = std::clamp(static_cast<float>(v), 0.20f, 1.00f);
            else if (k == "uiScale")
                cfg.uiScaleMul = std::clamp(static_cast<float>(v), 0.75f, 1.50f);
        }
        presenter.setGridVisible(cfg.grid);
        presenter.setFillOpacity(cfg.fillOpacity);
    }
    auto saveCfg = [&]() {
        std::ofstream o(cfgPath, std::ios::trunc);
        o << "grid " << (cfg.grid ? 1 : 0) << "\nlabels " << (cfg.labels ? 1 : 0)
          << "\nfillOpacity " << cfg.fillOpacity << "\nuiScale " << cfg.uiScaleMul << "\n";
    };

    // eased view animation: scroll/reset move TARGETS; the loop glides vp
    // toward them (smooth zoom, fly-home), exponential approach in log space.
    double targetWpp = vp.worldPerPixel, targetCx = vp.centerX, targetCy = vp.centerY;
    bool viewAnimating = false;
    float selAnim = 0.0f; // eased selection-highlight row
    auto animPrev = std::chrono::steady_clock::now();

    // sleeping chrome: attention eases grid/labels/HUD between wake and rest
    float wakeK = 1.0f;
    auto lastActivity = std::chrono::steady_clock::now();
    auto act = [&]() { lastActivity = std::chrono::steady_clock::now(); };
    // pan release-inertia (world units/s), fed by recent drag velocity
    double panVx = 0.0, panVy = 0.0;
    double dragVx = 0.0, dragVy = 0.0;
    auto lastDragT = std::chrono::steady_clock::now();

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

        // ---- animation step (smooth zoom / fly-home / selection glide) ----
        {
            const auto nowA = std::chrono::steady_clock::now();
            const double adt =
                std::clamp(std::chrono::duration<double>(nowA - animPrev).count(), 0.0, 0.05);
            animPrev = nowA;
            if (viewAnimating)
            {
                const double k = 1.0 - std::exp(-adt / 0.055);
                const double lw = std::log(vp.worldPerPixel) +
                                  (std::log(targetWpp) - std::log(vp.worldPerPixel)) * k;
                vp.worldPerPixel = std::exp(lw);
                vp.centerX += (targetCx - vp.centerX) * k;
                vp.centerY += (targetCy - vp.centerY) * k;
                if (std::abs(std::log(vp.worldPerPixel / targetWpp)) < 5e-4 &&
                    std::abs(targetCx - vp.centerX) < vp.worldPerPixel * 0.2 &&
                    std::abs(targetCy - vp.centerY) < vp.worldPerPixel * 0.2)
                {
                    vp.worldPerPixel = targetWpp;
                    vp.centerX = targetCx;
                    vp.centerY = targetCy;
                    viewAnimating = false;
                }
                viewportDirty = true;
            }
            const float selTarget = static_cast<float>(selected);
            selAnim += (selTarget - selAnim) *
                       static_cast<float>(1.0 - std::exp(-adt / 0.045));
            if (std::abs(selAnim - selTarget) < 0.004f) selAnim = selTarget;

            // pan inertia: glide after a flick, with exponential friction
            if (!dragging && (std::abs(panVx) > 1e-9 || std::abs(panVy) > 1e-9))
            {
                vp.centerX += panVx * adt;
                vp.centerY += panVy * adt;
                targetCx = vp.centerX;
                targetCy = vp.centerY;
                const double f = std::exp(-adt / 0.18);
                panVx *= f;
                panVy *= f;
                if (std::hypot(panVx, panVy) < vp.worldPerPixel * 3.0) panVx = panVy = 0.0;
                viewportDirty = true;
            }

            // sleeping chrome: ease toward rest after ~2.5s of stillness
            const double idleFor =
                std::chrono::duration<double>(nowA - lastActivity).count();
            const float wakeTarget = idleFor > 2.5 ? 0.0f : 1.0f;
            wakeK += (wakeTarget - wakeK) * static_cast<float>(1.0 - std::exp(-adt / 0.30));
            if (std::abs(wakeK - wakeTarget) < 0.005f) wakeK = wakeTarget;
            presenter.setChrome(wakeK, dpiScale);
        }
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
        if (cfg.labels) drawAxisNumbers(overlay, vp, fbW, fbH, 18.0f * uiS(), wakeK);
        drawUi(overlay, glass, fbW, fbH, uiS(), formulas, selected, selAnim, editing, editBuffer,
               editPos, status);
        {
            // live cursor coordinates (a plotter staple), bottom-right, quiet
            const double wx = vp.centerX + (mouseX - fbW * 0.5) * vp.worldPerPixel;
            const double wy = vp.centerY - (mouseY - fbH * 0.5) * vp.worldPerPixel;
            char cbuf[64];
            std::snprintf(cbuf, sizeof cbuf, "%.6g, %.6g", wx, wy);
            const float cs = uiS();
            const float cw = overlay.textWidth(cbuf, 0.72f);
            const float rx = fbW - cw - 16 * cs;
            // hide rather than collide with the centered hint pill on narrow windows
            if (rx > fbW * 0.5f + 260 * cs * 0.5f + 8 * cs)
                overlay.text(rx, fbH - 40 * cs + 5 * cs, cbuf, 0.72f,
                             {0.50f, 0.55f, 0.65f, 0.9f});
        }
        if (settingsOpen) drawSettings(overlay, glass, fbW, fbH, uiS(), cfg, settingsSel);
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
            drawDebug(overlay, glass, uiS(), vp, fbW, fbH, dbgTiles, engine.storeSize(), engine.jobsCompleted(), fps,
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
            if (wantScreenshot)
            {
                static int shotN = 0;
                const std::string path =
                    outDir + "/screenshot_" + std::to_string(++shotN) + ".png";
                saveFramebuffer(fbW, fbH, path);
                status = "saved " + path;
                wantScreenshot = false;
            }
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
        presenter.resize(fbW, fbH);
    glass.resize(fbW, fbH); // stores size; glViewport is set each renderFrame
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
                const bool was = dragging;
                dragging = (s == glfw::MouseButtonState::Press);
                auto [cx, cy] = w.getCursorPos();
                lastX = cx;
                lastY = cy;
                act();
                if (dragging)
                {
                    panVx = panVy = dragVx = dragVy = 0.0;
                    lastDragT = std::chrono::steady_clock::now();
                }
                else if (was)
                {
                    panVx = dragVx; // release: glide
                    panVy = dragVy;
                    dragVx = dragVy = 0.0;
                }
            }
        });

    window.cursorPosEvent.setCallback([&](glfw::Window &, double cx, double cy) {
        mouseX = cx;
        mouseY = cy;
        act();
        if (!dragging) return;
        const double dx = cx - lastX, dy = cy - lastY;
        lastX = cx;
        lastY = cy;
        vp.centerX -= dx * vp.worldPerPixel;
        vp.centerY += dy * vp.worldPerPixel; // cursor y is down, world y is up
        targetCx -= dx * vp.worldPerPixel;   // keep an in-flight zoom's target in step
        targetCy += dy * vp.worldPerPixel;
        const auto nowD = std::chrono::steady_clock::now();
        const double ddt =
            std::max(1e-4, std::chrono::duration<double>(nowD - lastDragT).count());
        lastDragT = nowD;
        dragVx = 0.6 * dragVx + 0.4 * (-dx * vp.worldPerPixel / ddt);
        dragVy = 0.6 * dragVy + 0.4 * (dy * vp.worldPerPixel / ddt);
        viewportDirty = true;
        markInput();
    });

    window.scrollEvent.setCallback([&](glfw::Window &w, double, double yoff) {
        // zoom moves the TARGET (anchored at the cursor); the render loop
        // glides toward it -- rapid scrolls accumulate into one smooth ride
        auto [cx, cy] = w.getCursorPos();
        const double worldX = targetCx + (cx - fbW * 0.5) * targetWpp;
        const double worldY = targetCy - (cy - fbH * 0.5) * targetWpp;
        const double factor = std::pow(1.1, -yoff);
        targetWpp *= factor;
        targetCx = worldX - (cx - fbW * 0.5) * targetWpp;
        targetCy = worldY + (cy - fbH * 0.5) * targetWpp;
        viewAnimating = true;
        viewportDirty = true;
        act();
        markInput();
    });

    window.keyEvent.setCallback(
        [&](glfw::Window &w, glfw::KeyCode key, int, glfw::KeyState action, glfw::ModifierKeyBit) {
            using K = glfw::KeyCode;
            const bool press = action == glfw::KeyState::Press;
            const bool held = press || action == glfw::KeyState::Repeat;

            if (settingsOpen)
            {
                if (!held) return;
                if (press && (key == K::Escape || key == K::S))
                {
                    settingsOpen = false;
                    saveCfg();
                    return;
                }
                if (key == K::Up) settingsSel = (settingsSel + 3) % 4;
                if (key == K::Down) settingsSel = (settingsSel + 1) % 4;
                const int dir = key == K::Right ? 1 : key == K::Left ? -1 : 0;
                if (dir != 0)
                {
                    switch (settingsSel)
                    {
                    case 0: if (press) cfg.grid = !cfg.grid; break;
                    case 1: if (press) cfg.labels = !cfg.labels; break;
                    case 2:
                        cfg.fillOpacity =
                            std::clamp(cfg.fillOpacity + 0.05f * static_cast<float>(dir), 0.20f, 1.00f);
                        break;
                    case 3:
                        cfg.uiScaleMul =
                            std::clamp(cfg.uiScaleMul + 0.125f * static_cast<float>(dir), 0.75f, 1.50f);
                        break;
                    }
                    presenter.setGridVisible(cfg.grid);
                    presenter.setFillOpacity(cfg.fillOpacity);
                }
                return;
            }
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
                        formulas[selected] = editBuffer;
                        rels[selected] = std::make_shared<const Relation>(std::move(*parsed));
                        engine.setRelations(rels);
                        editing = false;
                        status.clear();
                        std::printf("GraphXplorer: %s\n", formulas[selected].c_str());
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
                editBuffer = formulas[selected];
                editPos = editBuffer.size();
                status.clear();
                return;
            }
            if (key == K::Tab)
            {
                selected = (selected + 1) % formulas.size();
                return;
            }
            if (key == K::N && formulas.size() < 8)
            {
                formulas.push_back("y = x");
                rels.push_back(parseOrNull(formulas.back()));
                selected = formulas.size() - 1;
                engine.setRelations(rels);
                editing = true; // a new row goes straight into edit
                editBuffer = formulas[selected];
                editPos = editBuffer.size();
                status.clear();
                return;
            }
            if (key == K::X && formulas.size() > 1)
            {
                formulas.erase(formulas.begin() + static_cast<ptrdiff_t>(selected));
                rels.erase(rels.begin() + static_cast<ptrdiff_t>(selected));
                if (selected >= formulas.size()) selected = formulas.size() - 1;
                engine.setRelations(rels);
                status.clear();
                return;
            }
            if (key == K::R)
            {
                targetCx = targetCy = 0.0; // fly home, eased
                targetWpp = 16.0 / fbW;
                viewAnimating = true;
                viewportDirty = true;
                markInput();
                return;
            }
            if (key == K::S)
            {
                settingsOpen = true;
                settingsSel = 0;
                return;
            }
            if (key == K::P)
            {
                wantScreenshot = true; // captured at end of this frame
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
                if (auto r = parseOrNull(kPresets[idx]))
                {
                    formulas[selected] = kPresets[idx];
                    rels[selected] = r;
                    engine.setRelations(rels);
                    status.clear();
                    std::printf("GraphXplorer: %s\n", formulas[selected].c_str());
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
        else if (presenter.activeFades() > 0 || viewAnimating ||
                 std::abs(panVx) > 1e-12 || std::abs(panVy) > 1e-12 ||
                 (wakeK > 0.0f && wakeK < 1.0f) ||
                 std::abs(selAnim - static_cast<float>(selected)) > 0.004f)
            glfw::waitEvents(0.006); // crossfades / view & UI animations / glide
        else
            glfw::waitEvents(0.05);
        pendingWaitMs = msSince(w0); // includes event-callback dispatch time
        renderOnce();
    }
    return 0;
}

int runSelftest(const std::string &outPng, const std::string &formula, bool debug,
                int W, int H)
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
    Glass glass;
    float s = gSelftestScale;
    if (s <= 0.0f) s = std::get<0>(window.getContentScale());
    if (!(s >= 0.5f && s <= 4.0f)) s = 1.0f;
    Overlay overlay(findFont(), static_cast<int>(22.0f * s + 0.5f));
    auto [fbW, fbH] = window.getFramebufferSize();
    presenter.resize(fbW, fbH);
    glass.resize(fbW, fbH);
    overlay.resize(fbW, fbH);
    Viewport vp{0.0, 0.0, 16.0 / fbW, fbW, fbH};

    // semicolon-separated formulas land in successive relation slots
    std::vector<std::shared_ptr<const Relation>> rels;
    size_t pos = 0;
    while (pos <= formula.size())
    {
        const size_t semi = formula.find(';', pos);
        const std::string part =
            formula.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        if (part.find_first_not_of(" \t") != std::string::npos)
        {
            auto r = parseOrNull(part);
            if (!r) return 1;
            rels.push_back(std::move(r));
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    if (rels.empty()) return 1;
    engine.setRelations(rels);
    engine.setViewport(vp);

    std::vector<PresentTile> present;
    std::vector<DebugTile> dbgTiles;
    for (int f = 0; f < 200; ++f) // let workers solve coarse->fine
    {
        glfw::pollEvents();
        engine.buildPresent(vp, present);
        (void)presenter.renderFrame(vp, present, /*uploadBudget=*/64);
        drawAxisNumbers(overlay, vp, fbW, fbH, 18.0f * s, 1.0f);
        drawUi(overlay, glass, fbW, fbH, s, {formula}, 0, 0.0f, /*editing=*/false, "", 0, "");
        if (debug)
        {
            engine.debugTiles(vp, dbgTiles);
            drawDebug(overlay, glass, s, vp, fbW, fbH, dbgTiles, engine.storeSize(),
                      engine.jobsCompleted(), 120.0, 8.0, /*holes=*/0);
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
    Glass glass;
    auto rel = parseOrNull("y > sin(2^x)");
    if (!rel) return 1;
    engine.setRelation(rel);

    auto sz0 = window.getFramebufferSize();
    int fbW = std::get<0>(sz0), fbH = std::get<1>(sz0);
    presenter.resize(fbW, fbH);
    glass.resize(fbW, fbH);
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
        int lastPending = 0;
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
            lastPending = pending;
            if (frame % 300 == 299)
                std::printf("    [f%d] pend=%d up=%d(%.1fms) resident=%zu free=%zu fades=%d\n",
                            frame, pending, presenter.lastUploads(), presenter.lastUploadMs(),
                            presenter.residentLayers(), presenter.freeLayers(),
                            presenter.activeFades());
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
                    "pend=%-4d jobs=%-6llu store=%-5zu %s\n",
                    name.c_str(), fbW, fbH, vp.worldPerPixel, frame, fb, maxHoles, holeFrames,
                    lastPending, static_cast<unsigned long long>(engine.jobsCompleted()),
                    engine.storeSize(), frozen ? "*** FROZEN ***" : "ok");
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
    glass.resize(fbW, fbH);
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

    // (C) Working set > store budget: back on the oscillating band (everything
    // Mixed) at a depth where the draw set + pan-ahead ring + tree + history
    // exceed kResidencyTiles. The SOFT eviction cap must let the store grow
    // past the budget instead of evicting the active view (which thrashed
    // forever: permanently unfilled holes, observed live after frequent
    // resizing). Convergence == regression-free.
    vp.centerX = 15.0;
    vp.centerY = 0.3;
    vp.worldPerPixel = 0.0003;
    renderToCompletion("C-thrash");
    probe("C-thrash");

    std::printf(
        "(want: no FROZEN, fast* TRUEHOLE frames=0, freshOut GUARDBARE frames=0, B-* nonDone=0)\n");
    return 0;
}
