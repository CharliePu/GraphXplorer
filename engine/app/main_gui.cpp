// GraphXplorer2 - live CPU renderer. Pan = drag, zoom = scroll, 1-6 = preset
// formulas, R = reset view, Esc = quit. All GL on the main thread; all math async.

#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <glfwpp/glfwpp.h>

#include "app/Engine.h"
#include "image/Png.h"
#include "present/GlPresenter.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
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
}

// Render the live GL pipeline headlessly to a PNG (validates context, shaders,
// tile-texture upload and compositing end-to-end). Usage: --selftest out.png [formula]
int runSelftest(const std::string &outPng, const std::string &formula);

int main(int argc, char **argv)
{
    if (argc >= 2 && std::string(argv[1]) == "--selftest")
    {
        const std::string out = argc >= 3 ? argv[2] : "selftest.png";
        const std::string f = argc >= 4 ? argv[3] : kPresets[0];
        return runSelftest(out, f);
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
    Engine engine(tilePx);
    GlPresenter presenter(tilePx);

    auto [fbW, fbH] = window.getFramebufferSize();
    presenter.resize(fbW, fbH);

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

    window.framebufferSizeEvent.setCallback([&](glfw::Window &, int w, int h) {
        fbW = std::max(1, w);
        fbH = std::max(1, h);
        presenter.resize(fbW, fbH);
        vp.pxW = fbW;
        vp.pxH = fbH;
        viewportDirty = true;
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
            if (action != glfw::KeyState::Press) return;
            if (key == glfw::KeyCode::Escape) w.setShouldClose(true);
            if (key == glfw::KeyCode::R)
            {
                vp.centerX = vp.centerY = 0.0;
                vp.worldPerPixel = 16.0 / fbW;
                viewportDirty = true;
            }
            int idx = -1;
            using K = glfw::KeyCode;
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
                    std::printf("GraphXplorer2: %s\n", formula.c_str());
                }
            }
        });

    std::vector<PresentTile> present;
    while (!window.shouldClose())
    {
        glfw::pollEvents();
        if (viewportDirty)
        {
            engine.setViewport(vp);
            viewportDirty = false;
        }
        engine.buildPresent(vp, present);
        presenter.renderFrame(vp, present, /*uploadBudget=*/12);
        window.swapBuffers();
    }
    return 0;
}

int runSelftest(const std::string &outPng, const std::string &formula)
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
    auto [fbW, fbH] = window.getFramebufferSize();
    presenter.resize(fbW, fbH);
    Viewport vp{0.0, 0.0, 16.0 / fbW, fbW, fbH};

    auto rel = parseOrNull(formula);
    if (!rel) return 1;
    engine.setRelation(rel);
    engine.setViewport(vp);

    std::vector<PresentTile> present;
    for (int f = 0; f < 200; ++f) // let workers solve coarse->fine
    {
        glfw::pollEvents();
        engine.buildPresent(vp, present);
        presenter.renderFrame(vp, present, /*uploadBudget=*/64);
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
