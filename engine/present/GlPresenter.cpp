#include "GlPresenter.h"

#include <glad/glad.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace gxr
{
namespace
{
const char *kTileVs = R"(#version 330 core
layout(location=0) in vec2 uv;
uniform vec4 ndcRect; // x0,y0,x1,y1 on screen
uniform vec4 uvRect;  // texture sub-rect to sample (u0,v0,u1,v1)
out vec2 vUv;
void main(){
    vec2 p = mix(ndcRect.xy, ndcRect.zw, uv);
    gl_Position = vec4(p, 0.0, 1.0);
    vUv = mix(uvRect.xy, uvRect.zw, uv);
})";

const char *kTileFs = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D tex;
uniform vec3 fill;
uniform int flatMode;     // 1 = greedy uniform tile (no texture sample)
uniform float flatValue;  // coverage for a flat tile (0 or 1)
void main(){
    float c = (flatMode == 1) ? flatValue : texture(tex, vUv).r;
    if (c <= 0.0015) discard;
    frag = vec4(fill, c);
})";

const char *kLineVs = R"(#version 330 core
layout(location=0) in vec2 pos;
void main(){ gl_Position = vec4(pos, 0.0, 1.0); })";

const char *kLineFs = R"(#version 330 core
out vec4 frag;
uniform vec4 color;
void main(){ frag = color; })";

constexpr size_t kMaxResidentTextures = 1024;

void worldToNdc(const Viewport &vp, int fbW, int fbH, double wx, double wy, float &nx, float &ny)
{
    const double sx = (wx - vp.centerX) / vp.worldPerPixel + fbW * 0.5;
    const double sy = (wy - vp.centerY) / vp.worldPerPixel + fbH * 0.5;
    nx = static_cast<float>(sx / fbW * 2.0 - 1.0);
    ny = static_cast<float>(sy / fbH * 2.0 - 1.0);
}
}

GlPresenter::GlPresenter(int tilePx) : tilePx_(tilePx)
{
    tileProgram_ = compile(kTileVs, kTileFs);
    lineProgram_ = compile(kLineVs, kLineFs);
    uNdcRect_ = glGetUniformLocation(tileProgram_, "ndcRect");
    uUvRect_ = glGetUniformLocation(tileProgram_, "uvRect");
    uTex_ = glGetUniformLocation(tileProgram_, "tex");
    uFill_ = glGetUniformLocation(tileProgram_, "fill");
    uFlatMode_ = glGetUniformLocation(tileProgram_, "flatMode");
    uFlatValue_ = glGetUniformLocation(tileProgram_, "flatValue");
    uLineColor_ = glGetUniformLocation(lineProgram_, "color");

    // 1x1 white texture, bound for flat (textureless) draws so the sampler is valid.
    glGenTextures(1, &dummyTex_);
    glBindTexture(GL_TEXTURE_2D, dummyTex_);
    const float one = 1.0f;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 1, 1, 0, GL_RED, GL_FLOAT, &one);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    constexpr float quad[] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    glGenVertexArrays(1, &quadVao_);
    glGenBuffers(1, &quadVbo_);
    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glGenVertexArrays(1, &lineVao_);
    glGenBuffers(1, &lineVbo_);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

GlPresenter::~GlPresenter()
{
    for (auto &[k, t] : textures_) glDeleteTextures(1, &t.id);
    glDeleteTextures(1, &dummyTex_);
    glDeleteProgram(tileProgram_);
    glDeleteProgram(lineProgram_);
    glDeleteVertexArrays(1, &quadVao_);
    glDeleteBuffers(1, &quadVbo_);
    glDeleteVertexArrays(1, &lineVao_);
    glDeleteBuffers(1, &lineVbo_);
}

unsigned int GlPresenter::compile(const char *vs, const char *fs)
{
    auto make = [](GLenum type, const char *src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[1024];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::fprintf(stderr, "shader compile error: %s\n", log);
        }
        return s;
    };
    GLuint v = make(GL_VERTEX_SHADER, vs);
    GLuint f = make(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "program link error: %s\n", log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

void GlPresenter::resize(int fbWidth, int fbHeight)
{
    fbW_ = std::max(1, fbWidth);
    fbH_ = std::max(1, fbHeight);
}

unsigned int GlPresenter::ensureTexture(const CoverageTilePtr &cov, int &budget, uint64_t frame)
{
    if (!cov || cov->alpha.empty()) return 0;
    // Coverage tiles are immutable and uniquely identified by payloadId, so an
    // uploaded payload never needs re-uploading; just refresh its LRU stamp.
    const auto it = textures_.find(cov->payloadId);
    if (it != textures_.end())
    {
        it->second.lastFrame = frame;
        return it->second.id;
    }
    if (budget <= 0) return 0; // out of upload budget this frame -> caller falls back
    const auto t0 = std::chrono::steady_clock::now();
    TileTex tex;
    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, cov->width, cov->height, 0, GL_RED, GL_FLOAT,
                 cov->alpha.data());
    tex.lastFrame = frame;
    textures_.emplace(cov->payloadId, tex);
    --budget;
    ++uploads_;
    uploadMs_ +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return tex.id;
}

void GlPresenter::evictTextures(uint64_t frame)
{
    // Residency scales with the window: hold ~3 detail levels' worth of tiles so
    // zooming across a detail boundary reuses RESIDENT textures instead of having to
    // re-upload them (re-uploads are budget-limited -> tiles draw blank for a frame
    // = the "swap not atomic" holes). One detail level at a big window is ~1.5k tiles,
    // far above the old fixed 1024 cap.
    const size_t visibleTiles = static_cast<size_t>(fbW_ / tilePx_ + 2) *
                                static_cast<size_t>(fbH_ / tilePx_ + 2);
    const size_t cap = std::max<size_t>(kMaxResidentTextures, visibleTiles * 3);
    if (textures_.size() <= cap) return;
    std::vector<std::pair<uint64_t, uint64_t>> refs; // (lastFrame, payloadId)
    refs.reserve(textures_.size());
    for (const auto &[k, t] : textures_) refs.emplace_back(t.lastFrame, k);
    std::sort(refs.begin(), refs.end(), [](auto &a, auto &b) { return a.first < b.first; });
    size_t toRemove = textures_.size() - cap;
    for (size_t i = 0; i < refs.size() && toRemove > 0; ++i)
    {
        if (refs[i].first == frame) break; // don't evict tiles used this frame
        auto it = textures_.find(refs[i].second);
        if (it != textures_.end())
        {
            glDeleteTextures(1, &it->second.id);
            textures_.erase(it);
            --toRemove;
        }
    }
}

int GlPresenter::renderFrame(const Viewport &vp, const std::vector<PresentTile> &tiles,
                             int uploadBudget)
{
    ++frame_;
    int pendingUploads = 0;
    holeTiles_ = 0; // true holes this frame (drew nothing where content was expected)
    uploadMs_ = 0.0;
    uploads_ = 0;
    drawnTiles_ = 0;
    glViewport(0, 0, fbW_, fbH_);
    glClearColor(0.07f, 0.07f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ---- grid + axes ----
    {
        std::vector<float> verts;
        const WorldRect wb = vp.worldBounds();
        const double rawStep = vp.worldPerPixel * 90.0;
        const double mag = std::pow(10.0, std::floor(std::log10(std::max(rawStep, 1e-300))));
        const double norm = rawStep / mag;
        const double step = (norm < 2.0 ? 1.0 : norm < 5.0 ? 2.0 : 5.0) * mag;
        auto pushLine = [&](double x0, double y0, double x1, double y1) {
            float a, b, c, d;
            worldToNdc(vp, fbW_, fbH_, x0, y0, a, b);
            worldToNdc(vp, fbW_, fbH_, x1, y1, c, d);
            verts.insert(verts.end(), {a, b, c, d});
        };
        int guard = 0;
        for (double x = std::ceil(wb.x0 / step) * step; x <= wb.x1 && guard < 400; x += step, ++guard)
            pushLine(x, wb.y0, x, wb.y1);
        for (double y = std::ceil(wb.y0 / step) * step; y <= wb.y1 && guard < 800; y += step, ++guard)
            pushLine(wb.x0, y, wb.x1, y);
        const size_t gridCount = verts.size() / 2;
        // axes last (brighter)
        pushLine(0, wb.y0, 0, wb.y1);
        pushLine(wb.x0, 0, wb.x1, 0);

        glUseProgram(lineProgram_);
        glBindVertexArray(lineVao_);
        glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                     verts.data(), GL_DYNAMIC_DRAW);
        glUniform4f(uLineColor_, 0.18f, 0.18f, 0.21f, 1.0f);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(gridCount));
        glUniform4f(uLineColor_, 0.40f, 0.40f, 0.45f, 1.0f);
        glDrawArrays(GL_LINES, static_cast<GLsizei>(gridCount), 4);
    }

    // ---- coverage tiles (greedy quadtree leaves) ----
    glUseProgram(tileProgram_);
    glBindVertexArray(quadVao_);
    glUniform1i(uTex_, 0);
    glUniform3f(uFill_, 0.0f, 0.55f, 0.98f);
    glActiveTexture(GL_TEXTURE0);

    int budget = uploadBudget;
    for (const PresentTile &t : tiles)
    {
        unsigned int texId = 0;
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        if (t.flat)
        {
            if (t.flatValue <= 0.0f) continue; // proven-empty: draw nothing
            glUniform1i(uFlatMode_, 1);
            glUniform1f(uFlatValue_, t.flatValue);
            texId = dummyTex_;
        }
        else
        {
            texId = ensureTexture(t.cov, budget, frame_);
            if (texId != 0)
            {
                glUniform1i(uFlatMode_, 0);
                u0 = t.u0;
                v0 = t.v0;
                u1 = t.u1;
                v1 = t.v1;
            }
            else
            {
                // The tile's OWN texture isn't resident yet. Keep it on the upload
                // backlog (so the loop re-renders until it's crisp), but draw the
                // baked coarse-ancestor STAND-IN this frame instead of a hole --
                // "retain the old tile until the new one is ready". The stand-in is
                // one shared, already-resident ancestor payload (zero new upload), so
                // the region shows correct coarse content and sharpens in over the
                // next few frames as the own texture uploads.
                if (t.cov && !t.cov->alpha.empty()) ++pendingUploads;
                if (t.standinFlat)
                {
                    glUniform1i(uFlatMode_, 1);
                    glUniform1f(uFlatValue_, 1.0f);
                    texId = dummyTex_;
                }
                else if (t.standinCov && !t.standinCov->alpha.empty() &&
                         (texId = ensureTexture(t.standinCov, budget, frame_)) != 0)
                {
                    glUniform1i(uFlatMode_, 0);
                    u0 = t.su0;
                    v0 = t.sv0;
                    u1 = t.su1;
                    v1 = t.sv1;
                }
                else
                {
                    // No resident stand-in either (a genuinely-cold region) -> leave
                    // background. This is the only real hole, and it is rare.
                    ++holeTiles_;
                    continue;
                }
            }
        }
        float nx0, ny0, nx1, ny1;
        worldToNdc(vp, fbW_, fbH_, t.rect.x0, t.rect.y0, nx0, ny0);
        worldToNdc(vp, fbW_, fbH_, t.rect.x1, t.rect.y1, nx1, ny1);
        glUniform4f(uNdcRect_, nx0, ny0, nx1, ny1);
        glUniform4f(uUvRect_, u0, v0, u1, v1);
        glBindTexture(GL_TEXTURE_2D, texId);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        ++drawnTiles_;
    }

    evictTextures(frame_);
    glBindVertexArray(0);
    return pendingUploads;
}
}
