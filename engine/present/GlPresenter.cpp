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
layout(location=0) in vec2 corner;
layout(location=1) in vec4 iNdc;    // x0,y0,x1,y1 on screen
layout(location=2) in vec4 iUv;     // texture sub-rect (u0,v0,u1,v1)
layout(location=3) in vec4 iUvFrom; // crossfade-source sub-rect
layout(location=4) in vec4 iMisc;   // x=layer (<0 => flat), y=layerFrom, z=fade, w=flatValue
out vec3 vUv;
out vec3 vUvFrom;
out float vFade;
out float vFlat; // >=0: flat coverage value; <0: sample the texture
void main(){
    vec2 p = mix(iNdc.xy, iNdc.zw, corner);
    gl_Position = vec4(p, 0.0, 1.0);
    vUv = vec3(mix(iUv.xy, iUv.zw, corner), max(iMisc.x, 0.0));
    vUvFrom = vec3(mix(iUvFrom.xy, iUvFrom.zw, corner), max(iMisc.y, 0.0));
    vFade = iMisc.z;
    vFlat = iMisc.x < 0.0 ? iMisc.w : -1.0;
})";

const char *kTileFs = R"(#version 330 core
in vec3 vUv;
in vec3 vUvFrom;
in float vFade;
in float vFlat;
out vec4 frag;
uniform sampler2DArray tiles;
uniform vec3 fill;
void main(){
    float c = (vFlat >= 0.0) ? vFlat : texture(tiles, vUv).r;
    // Linear COVERAGE interpolation for crossfades: blending two stacked
    // translucent quads would dip mid-fade; mixing coverages is exact.
    if (vFade < 1.0) c = mix(texture(tiles, vUvFrom).r, c, vFade);
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

constexpr int kWantLayers = 4096;     // 4096 x 64x64 R8 = 16 MB, allocated once
constexpr double kUploadMsBudget = 3.0; // per-frame time budget for normal uploads
constexpr double kFadeMs = 120.0;       // refinement crossfade duration

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
    uFill_ = glGetUniformLocation(tileProgram_, "fill");
    uTiles_ = glGetUniformLocation(tileProgram_, "tiles");
    uLineColor_ = glGetUniformLocation(lineProgram_, "color");

    // The tile atlas: one R8 2D array; every upload from here on is a
    // glTexSubImage3D into this preallocated storage (no allocation churn).
    GLint maxLayers = 256;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
    layerCount_ = std::min<int>(kWantLayers, maxLayers);
    glGenTextures(1, &tileArray_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tileArray_);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, tilePx_, tilePx_, layerCount_, 0, GL_RED,
                 GL_UNSIGNED_BYTE, nullptr);
    for (int i = 0; i < layerCount_; ++i) freeLayers_.emplace_back(i, 0);

    constexpr float quad[] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    glGenVertexArrays(1, &quadVao_);
    glGenBuffers(1, &quadVbo_);
    glGenBuffers(1, &instVbo_);
    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    // per-instance attributes (locations 1..4), advancing once per instance
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    for (int loc = 1; loc <= 4; ++loc)
    {
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, sizeof(Inst),
                              reinterpret_cast<const void *>(static_cast<size_t>(loc - 1) * 16));
        glVertexAttribDivisor(loc, 1);
    }

    glGenVertexArrays(1, &lineVao_);
    glGenBuffers(1, &lineVbo_);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

GlPresenter::~GlPresenter()
{
    glDeleteTextures(1, &tileArray_);
    glDeleteProgram(tileProgram_);
    glDeleteProgram(lineProgram_);
    glDeleteVertexArrays(1, &quadVao_);
    glDeleteBuffers(1, &quadVbo_);
    glDeleteBuffers(1, &instVbo_);
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

int GlPresenter::ensureLayer(const CoverageTilePtr &cov, int &budget, int &criticalLeft,
                             uint64_t frame, bool critical)
{
    if (!cov || cov->alpha.empty()) return -1;
    if (cov->width != tilePx_ || cov->height != tilePx_) return -1; // atlas is tilePx-only
    const auto it = layers_.find(cov->payloadId);
    if (it != layers_.end())
    {
        it->second.lastFrame = frame; // residency is never rationed
        return it->second.layer;
    }
    if (critical)
    {
        if (criticalLeft <= 0) return -1;
        --criticalLeft;
    }
    else if (budget <= 0 || uploadMs_ >= kUploadMsBudget)
    {
        return -1;
    }
    // Reuse only layers released a few frames ago (no implicit driver sync).
    constexpr uint64_t kPoolAgeFrames = 3;
    if (freeLayers_.empty()) return -1; // atlas full this frame; eviction frees soon
    if (frame - freeLayers_.front().second < kPoolAgeFrames && !critical) return -1;

    const auto t0 = std::chrono::steady_clock::now();
    const int layer = freeLayers_.front().first;
    freeLayers_.pop_front();
    const size_t count = cov->alpha.size();
    upload8_.resize(count);
    for (size_t i = 0; i < count; ++i)
    {
        const float a = cov->alpha[i];
        upload8_[i] = static_cast<unsigned char>(
            a <= 0.0f ? 0 : a >= 1.0f ? 255 : static_cast<int>(a * 255.0f + 0.5f));
    }
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, tilePx_, tilePx_, 1, GL_RED,
                    GL_UNSIGNED_BYTE, upload8_.data());
    layers_.emplace(cov->payloadId, TileTex{layer, frame});
    --budget;
    ++uploads_;
    uploadMs_ +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return layer;
}

void GlPresenter::evictLayers(uint64_t frame)
{
    // Residency scales with the window: hold ~3 detail levels' worth of tiles so
    // zooming across a detail boundary reuses RESIDENT layers instead of having
    // to re-upload them. Bounded by the atlas size.
    const size_t visibleTiles = static_cast<size_t>(fbW_ / tilePx_ + 2) *
                                static_cast<size_t>(fbH_ / tilePx_ + 2);
    const size_t cap =
        std::min<size_t>(static_cast<size_t>(layerCount_) * 3 / 4,
                         std::max<size_t>(1024, visibleTiles * 3));
    if (layers_.size() <= cap) return;
    std::vector<std::pair<uint64_t, uint64_t>> refs; // (lastFrame, payloadId)
    refs.reserve(layers_.size());
    for (const auto &[k, t] : layers_) refs.emplace_back(t.lastFrame, k);
    std::sort(refs.begin(), refs.end(), [](auto &a, auto &b) { return a.first < b.first; });
    size_t toRemove = layers_.size() - cap;
    for (size_t i = 0; i < refs.size() && toRemove > 0; ++i)
    {
        if (refs[i].first == frame) break; // don't evict layers used this frame
        auto it = layers_.find(refs[i].second);
        if (it != layers_.end())
        {
            freeLayers_.emplace_back(it->second.layer, frame);
            layers_.erase(it);
            --toRemove;
        }
    }
}

int GlPresenter::renderFrame(const Viewport &vp, const std::vector<PresentTile> &tiles,
                             int uploadBudget)
{
    ++frame_;
    int pendingUploads = 0;
    holeTiles_ = 0;
    uploadMs_ = 0.0;
    uploads_ = 0;
    drawnTiles_ = 0;
    fadesActive_ = 0;
    const double nowMs = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
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

    // ---- coverage tiles: build the instance list, then ONE instanced draw ----
    glBindTexture(GL_TEXTURE_2D_ARRAY, tileArray_);
    int budget = uploadBudget;
    int criticalLeft = 64;
    insts_.clear();
    insts_.reserve(tiles.size());

    for (const PresentTile &t : tiles)
    {
        Inst inst{};
        inst.misc[2] = 1.0f; // fade: steady state
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;

        if (t.flat)
        {
            if (t.flatValue <= 0.0f) continue; // proven-empty: draw nothing
            inst.misc[0] = -1.0f;
            inst.misc[3] = t.flatValue;
        }
        else
        {
            // Selection order (no-downgrade + no-hole, cheapest-correct first):
            //   1. own payload (resident, or uploaded within the normal budgets)
            //   2. this key's PREVIOUSLY DRAWN payload (resident, zero upload)
            //   3. proven-true flat stand-in
            //   4. ancestor stand-in payload (resident or budgeted upload)
            //   5. the ancestor key's previously drawn payload (uv composed)
            //   6. CRITICAL (budget-exempt, bounded) upload: own cov, else stand-in
            //   7. background -- only where nothing was ever drawn (cold region)
            const bool haveCov = t.cov && !t.cov->alpha.empty();
            const bool haveStandin = t.standinCov && !t.standinCov->alpha.empty();
            uint64_t drawnPayload = 0;
            bool ownTex = false;
            bool flatStandin = false;
            const Shown *prior = nullptr;
            if (const auto ls = lastShown_.find(t.key); ls != lastShown_.end())
                prior = &ls->second;

            int layer = ensureLayer(t.cov, budget, criticalLeft, frame_, /*critical=*/false);
            if (layer >= 0)
            {
                ownTex = true;
                drawnPayload = t.cov->payloadId;
                u0 = t.u0; v0 = t.v0; u1 = t.u1; v1 = t.v1;
            }
            if (layer < 0 && prior)
            {
                const auto tex = layers_.find(prior->payload);
                if (tex != layers_.end())
                {
                    tex->second.lastFrame = frame_;
                    layer = tex->second.layer;
                    drawnPayload = prior->payload;
                    u0 = prior->u0; v0 = prior->v0;
                    u1 = prior->u1; v1 = prior->v1;
                }
                else
                {
                    lastShown_.erase(t.key);
                    prior = nullptr;
                }
            }
            if (layer < 0 && t.standinFlat) flatStandin = true;
            if (layer < 0 && !flatStandin && haveStandin)
            {
                layer = ensureLayer(t.standinCov, budget, criticalLeft, frame_, /*critical=*/false);
                if (layer >= 0)
                {
                    drawnPayload = t.standinCov->payloadId;
                    u0 = t.su0; v0 = t.sv0; u1 = t.su1; v1 = t.sv1;
                }
                else
                {
                    const auto ls = lastShown_.find(t.standinKey);
                    if (ls != lastShown_.end())
                    {
                        const auto tex = layers_.find(ls->second.payload);
                        if (tex != layers_.end())
                        {
                            // Compose this tile's sub-rect within whatever the
                            // ancestor key last showed (world-aligned -> exact).
                            tex->second.lastFrame = frame_;
                            layer = tex->second.layer;
                            drawnPayload = ls->second.payload;
                            const float aw = ls->second.u1 - ls->second.u0;
                            const float ah = ls->second.v1 - ls->second.v0;
                            u0 = ls->second.u0 + aw * t.su0;
                            v0 = ls->second.v0 + ah * t.sv0;
                            u1 = ls->second.u0 + aw * t.su1;
                            v1 = ls->second.v0 + ah * t.sv1;
                        }
                        else
                        {
                            lastShown_.erase(t.standinKey);
                        }
                    }
                }
            }
            if (layer < 0 && !flatStandin && haveCov)
            {
                layer = ensureLayer(t.cov, budget, criticalLeft, frame_, /*critical=*/true);
                if (layer >= 0)
                {
                    ownTex = true;
                    drawnPayload = t.cov->payloadId;
                    u0 = t.u0; v0 = t.v0; u1 = t.u1; v1 = t.v1;
                }
            }
            if (layer < 0 && !flatStandin && haveStandin)
            {
                layer = ensureLayer(t.standinCov, budget, criticalLeft, frame_, /*critical=*/true);
                if (layer >= 0)
                {
                    drawnPayload = t.standinCov->payloadId;
                    u0 = t.su0; v0 = t.sv0; u1 = t.su1; v1 = t.sv1;
                }
            }

            if (flatStandin)
            {
                if (haveCov) ++pendingUploads; // own texture still owed
                inst.misc[0] = -1.0f;
                inst.misc[3] = 1.0f;
            }
            else if (layer < 0)
            {
                // Nothing drawable: a never-drawn region (or the critical budget
                // ran dry in a cold storm) -> background, which is what this
                // region showed before anyway.
                ++holeTiles_;
                continue;
            }
            else
            {
                if (!ownTex && haveCov) ++pendingUploads; // own texture still owed
                inst.misc[0] = static_cast<float>(layer);

                // ---- refinement crossfade: melt, don't pop -------------------
                if (prior && prior->payload != drawnPayload && !fades_.count(t.key))
                    fades_.emplace(t.key, Fade{prior->payload, prior->u0, prior->v0, prior->u1,
                                               prior->v1, nowMs});
                if (const auto f = fades_.find(t.key); f != fades_.end())
                {
                    const double tt = (nowMs - f->second.t0) / kFadeMs;
                    const auto ftex = layers_.find(f->second.payload);
                    if (tt >= 1.0 || ftex == layers_.end() || f->second.payload == drawnPayload)
                    {
                        fades_.erase(f);
                    }
                    else
                    {
                        ftex->second.lastFrame = frame_; // keep the source resident
                        inst.misc[1] = static_cast<float>(ftex->second.layer);
                        inst.misc[2] = static_cast<float>(tt);
                        inst.uvFrom[0] = f->second.u0;
                        inst.uvFrom[1] = f->second.v0;
                        inst.uvFrom[2] = f->second.u1;
                        inst.uvFrom[3] = f->second.v1;
                        ++fadesActive_;
                    }
                }

                lastShown_[t.key] = Shown{drawnPayload, u0, v0, u1, v1};
            }
        }

        worldToNdc(vp, fbW_, fbH_, t.rect.x0, t.rect.y0, inst.ndc[0], inst.ndc[1]);
        worldToNdc(vp, fbW_, fbH_, t.rect.x1, t.rect.y1, inst.ndc[2], inst.ndc[3]);
        inst.uv[0] = u0;
        inst.uv[1] = v0;
        inst.uv[2] = u1;
        inst.uv[3] = v1;
        insts_.push_back(inst);
    }

    if (!insts_.empty())
    {
        glUseProgram(tileProgram_);
        glUniform1i(uTiles_, 0);
        glUniform3f(uFill_, 0.0f, 0.55f, 0.98f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tileArray_);
        glBindVertexArray(quadVao_);
        glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
        const GLsizeiptr bytes = static_cast<GLsizeiptr>(insts_.size() * sizeof(Inst));
        glBufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_STREAM_DRAW); // orphan
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, insts_.data());
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(insts_.size()));
        drawnTiles_ = static_cast<int>(insts_.size());
    }

    evictLayers(frame_);
    if (lastShown_.size() > 16384) lastShown_.clear();
    if (fades_.size() > 4096) fades_.clear();
    glBindVertexArray(0);
    return pendingUploads;
}
}
