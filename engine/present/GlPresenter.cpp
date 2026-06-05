#include "GlPresenter.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace gxr
{
namespace
{
const char *kTileVs = R"(#version 330 core
layout(location=0) in vec2 uv;
uniform vec4 ndcRect; // x0,y0,x1,y1
out vec2 vUv;
void main(){
    vec2 p = mix(ndcRect.xy, ndcRect.zw, uv);
    gl_Position = vec4(p, 0.0, 1.0);
    vUv = uv;
})";

const char *kTileFs = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D tex;
uniform vec3 fill;
void main(){
    float c = texture(tex, vUv).r;
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
    uTex_ = glGetUniformLocation(tileProgram_, "tex");
    uFill_ = glGetUniformLocation(tileProgram_, "fill");
    uLineColor_ = glGetUniformLocation(lineProgram_, "color");

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

void GlPresenter::ensureTexture(const PresentTile &t, int &budget, uint64_t frame)
{
    if (!t.cov) return;
    auto it = textures_.find(t.key);
    if (it != textures_.end() && it->second.payload == t.cov->payloadId)
    {
        it->second.lastFrame = frame; // already current
        return;
    }
    if (budget <= 0 && it == textures_.end()) return; // no texture yet, out of budget

    const int w = t.cov->width, h = t.cov->height;
    if (it == textures_.end())
    {
        TileTex tex;
        glGenTextures(1, &tex.id);
        glBindTexture(GL_TEXTURE_2D, tex.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, t.cov->alpha.data());
        tex.payload = t.cov->payloadId;
        tex.lastFrame = frame;
        textures_.emplace(t.key, tex);
        --budget;
        return;
    }
    if (budget <= 0) return; // keep stale texture this frame
    glBindTexture(GL_TEXTURE_2D, it->second.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_FLOAT, t.cov->alpha.data());
    it->second.payload = t.cov->payloadId;
    it->second.lastFrame = frame;
    --budget;
}

void GlPresenter::evictTextures(uint64_t frame)
{
    if (textures_.size() <= kMaxResidentTextures) return;
    std::vector<std::pair<uint64_t, TileKey>> refs;
    refs.reserve(textures_.size());
    for (const auto &[k, t] : textures_) refs.emplace_back(t.lastFrame, k);
    std::sort(refs.begin(), refs.end(), [](auto &a, auto &b) { return a.first < b.first; });
    size_t toRemove = textures_.size() - kMaxResidentTextures;
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

void GlPresenter::renderFrame(const Viewport &vp, const std::vector<PresentTile> &tiles,
                              int uploadBudget)
{
    ++frame_;
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

    // ---- coverage tiles ----
    glUseProgram(tileProgram_);
    glBindVertexArray(quadVao_);
    glUniform1i(uTex_, 0);
    glUniform3f(uFill_, 0.0f, 0.55f, 0.98f);
    glActiveTexture(GL_TEXTURE0);

    int budget = uploadBudget;
    for (const PresentTile &t : tiles)
    {
        ensureTexture(t, budget, frame_);
        auto it = textures_.find(t.key);
        if (it == textures_.end()) continue; // no texture yet this frame
        float nx0, ny0, nx1, ny1;
        worldToNdc(vp, fbW_, fbH_, t.rect.x0, t.rect.y0, nx0, ny0);
        worldToNdc(vp, fbW_, fbH_, t.rect.x1, t.rect.y1, nx1, ny1);
        glUniform4f(uNdcRect_, nx0, ny0, nx1, ny1);
        glBindTexture(GL_TEXTURE_2D, it->second.id);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    evictTextures(frame_);
    glBindVertexArray(0);
}
}
