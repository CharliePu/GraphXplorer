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
layout(location=4) in vec4 iMisc;   // x=layer (<0 => flat), y=layerFrom, z=fade,
                                    // w=flatValue (flat) / band weight (textured)
out vec3 vUv;
out vec3 vUvFrom;
out float vFade;
out float vFlat; // >=0: flat coverage value; <0: sample the texture
out float vDim;  // textured band weight (dimmed under vector strokes)
void main(){
    vec2 p = mix(iNdc.xy, iNdc.zw, corner);
    gl_Position = vec4(p, 0.0, 1.0);
    vUv = vec3(mix(iUv.xy, iUv.zw, corner), max(iMisc.x, 0.0));
    vUvFrom = vec3(mix(iUvFrom.xy, iUvFrom.zw, corner), max(iMisc.y, 0.0));
    vFade = iMisc.z;
    vFlat = iMisc.x < 0.0 ? iMisc.w : -1.0;
    vDim = iMisc.x < 0.0 ? 1.0 : iMisc.w;
})";

const char *kTileFs = R"(#version 330 core
in vec3 vUv;
in vec3 vUvFrom;
in float vFade;
in float vFlat;
in float vDim;
out vec4 frag;
uniform sampler2DArray tiles;     // the bucket's own-content array
uniform sampler2DArray tilesFrom; // the bucket's crossfade-source array
uniform vec3 fill;
void main(){
    float c = (vFlat >= 0.0) ? vFlat : texture(tiles, vUv).r;
    // Linear COVERAGE interpolation for crossfades: blending two stacked
    // translucent quads would dip mid-fade; mixing coverages is exact.
    if (vFade < 1.0) c = mix(texture(tilesFrom, vUvFrom).r, c, vFade);
    if (vFlat < 0.0) c *= vDim; // band softened under vector strokes
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

// Equality-curve strokes: each segment instance expands into a screen-space
// quad of constant pixel width with square feathered caps.
const char *kSegVs = R"(#version 330 core
layout(location=0) in vec2 corner;  // (0..1)^2
layout(location=1) in vec4 iSeg;    // endpoint NDC: x0,y0,x1,y1
layout(location=2) in float iAlpha; // per-tile stroke weight (saturation ramp)
uniform vec2 pxOfNdc; // pixels per NDC unit (fbW/2, fbH/2)
uniform float halfW;  // half stroke width in pixels
out float vEdge;
out float vAlpha;
void main(){
    vec2 aPx = iSeg.xy * pxOfNdc;
    vec2 bPx = iSeg.zw * pxOfNdc;
    vec2 d = bPx - aPx;
    float len = max(length(d), 1e-6);
    vec2 t = d / len;
    vec2 n = vec2(-t.y, t.x);
    float along = corner.x * (len + 2.0 * halfW) - halfW;
    float across = (corner.y * 2.0 - 1.0) * halfW;
    vec2 p = aPx + t * along + n * across;
    gl_Position = vec4(p / pxOfNdc, 0.0, 1.0);
    vEdge = corner.y * 2.0 - 1.0;
    vAlpha = iAlpha;
})";

const char *kSegFs = R"(#version 330 core
in float vEdge;
in float vAlpha;
out vec4 frag;
uniform vec4 segColor;
uniform float halfW;
void main(){
    float a = clamp((1.0 - abs(vEdge)) * halfW, 0.0, 1.0); // ~1px rim feather
    frag = vec4(segColor.rgb, segColor.a * a * vAlpha);
})";

constexpr int kWantLayers = 8192;     // 8192 x 64x64 R8 = 32 MB, allocated once
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
    segProgram_ = compile(kSegVs, kSegFs);
    uFill_ = glGetUniformLocation(tileProgram_, "fill");
    uTiles_ = glGetUniformLocation(tileProgram_, "tiles");
    uTilesFrom_ = glGetUniformLocation(tileProgram_, "tilesFrom");
    uLineColor_ = glGetUniformLocation(lineProgram_, "color");
    uSegPx_ = glGetUniformLocation(segProgram_, "pxOfNdc");
    uSegHalfW_ = glGetUniformLocation(segProgram_, "halfW");
    uSegColor_ = glGetUniformLocation(segProgram_, "segColor");

    // The tile atlas: enough R8 2D arrays for kWantLayers total slots (drivers
    // commonly clamp layers per array to 2048, below a dense 4K view's working
    // set). Storage is allocated once; every upload from here on is a
    // glTexSubImage3D into existing memory (no allocation churn).
    GLint maxLayers = 256;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
    layersPerArray_ = std::min<int>(kWantLayers, maxLayers);
    const int numArrays =
        std::min(8, (kWantLayers + layersPerArray_ - 1) / layersPerArray_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int a = 0; a < numArrays; ++a)
    {
        unsigned int tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, tilePx_, tilePx_, layersPerArray_, 0, GL_RED,
                     GL_UNSIGNED_BYTE, nullptr);
        tileArrays_.push_back(tex);
    }
    slotCount_ = static_cast<int>(tileArrays_.size()) * layersPerArray_;
    for (int i = 0; i < slotCount_; ++i) freeLayers_.emplace_back(i, 0);
    buckets_.resize(tileArrays_.size() * tileArrays_.size());

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

    // stroke VAO: shared unit-quad corners + per-instance segment endpoints
    // and stroke weight (5 floats per instance)
    glGenVertexArrays(1, &segVao_);
    glGenBuffers(1, &segVbo_);
    glBindVertexArray(segVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, segVbo_);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<const void *>(4 * sizeof(float)));
    glVertexAttribDivisor(2, 1);
    glBindVertexArray(0);
}

GlPresenter::~GlPresenter()
{
    for (unsigned int tex : tileArrays_) glDeleteTextures(1, &tex);
    glDeleteProgram(tileProgram_);
    glDeleteProgram(lineProgram_);
    glDeleteProgram(segProgram_);
    glDeleteVertexArrays(1, &quadVao_);
    glDeleteBuffers(1, &quadVbo_);
    glDeleteBuffers(1, &instVbo_);
    glDeleteVertexArrays(1, &lineVao_);
    glDeleteBuffers(1, &lineVbo_);
    glDeleteVertexArrays(1, &segVao_);
    glDeleteBuffers(1, &segVbo_);
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

int GlPresenter::ensureSlot(const CoverageTilePtr &cov, int &budget, int &criticalLeft,
                            uint64_t frame, bool critical)
{
    if (!cov || cov->alpha.empty()) return -1;
    if (cov->width != tilePx_ || cov->height != tilePx_) return -1; // atlas is tilePx-only
    const auto it = layers_.find(cov->payloadId);
    if (it != layers_.end())
    {
        it->second.lastFrame = frame; // residency is never rationed
        return it->second.slot;
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
    // Reuse only slots released a few frames ago (no implicit driver sync).
    constexpr uint64_t kPoolAgeFrames = 3;
    if (freeLayers_.empty()) return -1; // atlas full this frame; eviction frees soon
    if (frame - freeLayers_.front().second < kPoolAgeFrames && !critical) return -1;

    const auto t0 = std::chrono::steady_clock::now();
    const int slot = freeLayers_.front().first;
    freeLayers_.pop_front();
    const size_t count = cov->alpha.size();
    upload8_.resize(count);
    for (size_t i = 0; i < count; ++i)
    {
        const float a = cov->alpha[i];
        upload8_[i] = static_cast<unsigned char>(
            a <= 0.0f ? 0 : a >= 1.0f ? 255 : static_cast<int>(a * 255.0f + 0.5f));
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, tileArrays_[static_cast<size_t>(slot / layersPerArray_)]);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, slot % layersPerArray_, tilePx_, tilePx_, 1,
                    GL_RED, GL_UNSIGNED_BYTE, upload8_.data());
    layers_.emplace(cov->payloadId, TileTex{slot, frame});
    --budget;
    ++uploads_;
    uploadMs_ +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return slot;
}

void GlPresenter::evictSlots(uint64_t frame)
{
    // Residency scales with the window: hold ~3 detail levels' worth of tiles so
    // zooming across a detail boundary reuses RESIDENT slots instead of having
    // to re-upload them. Bounded by the atlas size minus headroom for the aged
    // free pool -- a cap below the live demand (own + stand-in + fade payloads
    // of a huge dense view) starved finality with permanent pending uploads.
    const size_t visibleTiles = static_cast<size_t>(fbW_ / tilePx_ + 2) *
                                static_cast<size_t>(fbH_ / tilePx_ + 2);
    const size_t headroom = std::max<size_t>(256, static_cast<size_t>(slotCount_) / 16);
    const size_t cap = std::min<size_t>(static_cast<size_t>(slotCount_) - headroom,
                                        std::max<size_t>(1024, visibleTiles * 3));
    if (layers_.size() <= cap) return;
    std::vector<std::pair<uint64_t, uint64_t>> refs; // (lastFrame, payloadId)
    refs.reserve(layers_.size());
    for (const auto &[k, t] : layers_) refs.emplace_back(t.lastFrame, k);
    std::sort(refs.begin(), refs.end(), [](auto &a, auto &b) { return a.first < b.first; });
    size_t toRemove = layers_.size() - cap;
    for (size_t i = 0; i < refs.size() && toRemove > 0; ++i)
    {
        // Never evict recently-used layers (incl. freshly-uploaded ones not yet
        // drawn): under atlas pressure that cycled upload->evict->re-upload
        // forever and finality never latched. Soft cap, like the tile store.
        if (refs[i].first + 4 >= frame) break;
        auto it = layers_.find(refs[i].second);
        if (it != layers_.end())
        {
            freeLayers_.emplace_back(it->second.slot, frame);
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

    // ---- coverage tiles: build per-(array,fadeArray) instance buckets, then
    // one instanced draw per bucket (usually a single bucket) ----------------
    int budget = uploadBudget;
    int criticalLeft = 64;
    const int nArr = static_cast<int>(tileArrays_.size());
    for (auto &b : buckets_) b.clear();
    segScratch_.clear();
    // Equality-curve strokes for a tile drawing its OWN raster: transform the
    // tile-local segments to NDC.
    auto appendSegs = [&](const PresentTile &t) {
        const std::vector<float> &sg = t.cov->segs;
        const float sa = t.cov->strokeAlpha;
        const double w = t.rect.x1 - t.rect.x0, h = t.rect.y1 - t.rect.y0;
        for (size_t i = 0; i + 3 < sg.size(); i += 4)
        {
            float ax, ay, bx, by;
            worldToNdc(vp, fbW_, fbH_, t.rect.x0 + sg[i] * w, t.rect.y0 + sg[i + 1] * h, ax, ay);
            worldToNdc(vp, fbW_, fbH_, t.rect.x0 + sg[i + 2] * w, t.rect.y0 + sg[i + 3] * h, bx, by);
            if (ax == bx && ay == by) continue;
            segScratch_.insert(segScratch_.end(), {ax, ay, bx, by, sa});
        }
    };
    // A STAND-IN's segments: the ancestor's vector data is resolution-
    // independent, so a draft can show crisp (coarser-solved) curves instead
    // of a magnified raster band. Clip each segment to the displayed sub-rect
    // [su0,su1]x[sv0,sv1] and map it onto the tile's footprint.
    auto appendSegsMapped = [&](const PresentTile &t) {
        const std::vector<float> &sg = t.standinCov->segs;
        const double w = t.rect.x1 - t.rect.x0, h = t.rect.y1 - t.rect.y0;
        const double iu = 1.0 / (t.su1 - t.su0), iv = 1.0 / (t.sv1 - t.sv0);
        for (size_t i = 0; i + 3 < sg.size(); i += 4)
        {
            double x0 = sg[i], y0 = sg[i + 1], x1 = sg[i + 2], y1 = sg[i + 3];
            // Liang-Barsky clip to the sub-rect
            double t0 = 0.0, t1 = 1.0;
            const double dx = x1 - x0, dy = y1 - y0;
            const double p[4] = {-dx, dx, -dy, dy};
            const double q[4] = {x0 - t.su0, t.su1 - x0, y0 - t.sv0, t.sv1 - y0};
            bool reject = false;
            for (int e = 0; e < 4 && !reject; ++e)
            {
                if (p[e] == 0.0)
                    reject = q[e] < 0.0;
                else
                {
                    const double r = q[e] / p[e];
                    if (p[e] < 0.0)
                        t0 = std::max(t0, r);
                    else
                        t1 = std::min(t1, r);
                    reject = t0 > t1;
                }
            }
            if (reject) continue;
            const double cx0 = x0 + t0 * dx, cy0 = y0 + t0 * dy;
            const double cx1 = x0 + t1 * dx, cy1 = y0 + t1 * dy;
            float ax, ay, bx, by;
            worldToNdc(vp, fbW_, fbH_, t.rect.x0 + (cx0 - t.su0) * iu * w,
                       t.rect.y0 + (cy0 - t.sv0) * iv * h, ax, ay);
            worldToNdc(vp, fbW_, fbH_, t.rect.x0 + (cx1 - t.su0) * iu * w,
                       t.rect.y0 + (cy1 - t.sv0) * iv * h, bx, by);
            if (ax == bx && ay == by) continue;
            segScratch_.insert(segScratch_.end(), {ax, ay, bx, by, t.standinCov->strokeAlpha});
        }
    };

    for (const PresentTile &t : tiles)
    {
        Inst inst{};
        inst.misc[2] = 1.0f; // fade: steady state
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        int ownArr = 0, fadeArr = -1; // bucket key (flat tiles land in (0,0))
        bool segsDrawn = false;

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

            int slot = ensureSlot(t.cov, budget, criticalLeft, frame_, /*critical=*/false);
            if (slot >= 0)
            {
                ownTex = true;
                drawnPayload = t.cov->payloadId;
                u0 = t.u0; v0 = t.v0; u1 = t.u1; v1 = t.v1;
            }
            if (slot < 0 && prior)
            {
                const auto tex = layers_.find(prior->payload);
                if (tex != layers_.end())
                {
                    tex->second.lastFrame = frame_;
                    slot = tex->second.slot;
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
            if (slot < 0 && t.standinFlat) flatStandin = true;
            if (slot < 0 && !flatStandin && haveStandin)
            {
                slot = ensureSlot(t.standinCov, budget, criticalLeft, frame_, /*critical=*/false);
                if (slot >= 0)
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
                            slot = tex->second.slot;
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
            if (slot < 0 && !flatStandin && haveCov)
            {
                slot = ensureSlot(t.cov, budget, criticalLeft, frame_, /*critical=*/true);
                if (slot >= 0)
                {
                    ownTex = true;
                    drawnPayload = t.cov->payloadId;
                    u0 = t.u0; v0 = t.v0; u1 = t.u1; v1 = t.v1;
                }
            }
            if (slot < 0 && !flatStandin && haveStandin)
            {
                slot = ensureSlot(t.standinCov, budget, criticalLeft, frame_, /*critical=*/true);
                if (slot >= 0)
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
            else if (slot < 0)
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
                ownArr = slot / layersPerArray_;
                inst.misc[0] = static_cast<float>(slot % layersPerArray_);
                inst.misc[3] = 1.0f; // textured: full band weight by default

                // ---- refinement crossfade: melt, don't pop -------------------
                if (prior && prior->payload != drawnPayload && !fades_.count(t.key))
                {
                    fades_.emplace(t.key, Fade{prior->payload, prior->u0, prior->v0, prior->u1,
                                               prior->v1, nowMs});
                }
                else if (!prior && !fades_.count(t.key))
                {
                    // LEVEL-TRANSITION fade-in: this key never drew before, but
                    // its region was just showing an ancestor stand-in. Melt
                    // from that content (uv-composed to this tile's footprint)
                    // instead of popping -- immersion across scale changes.
                    const auto ls = lastShown_.find(t.standinKey);
                    if (ls != lastShown_.end() && ls->second.payload != drawnPayload)
                    {
                        const float aw = ls->second.u1 - ls->second.u0;
                        const float ah = ls->second.v1 - ls->second.v0;
                        fades_.emplace(t.key, Fade{ls->second.payload,
                                                   ls->second.u0 + aw * t.su0,
                                                   ls->second.v0 + ah * t.sv0,
                                                   ls->second.u0 + aw * t.su1,
                                                   ls->second.v0 + ah * t.sv1, nowMs});
                    }
                }
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
                        fadeArr = ftex->second.slot / layersPerArray_;
                        inst.misc[1] = static_cast<float>(ftex->second.slot % layersPerArray_);
                        inst.misc[2] = static_cast<float>(tt);
                        inst.uvFrom[0] = f->second.u0;
                        inst.uvFrom[1] = f->second.v0;
                        inst.uvFrom[2] = f->second.u1;
                        inst.uvFrom[3] = f->second.v1;
                        ++fadesActive_;
                    }
                }

                lastShown_[t.key] = Shown{drawnPayload, u0, v0, u1, v1};
                // Per-frame stroke budget: beyond it a tile keeps its band quad
                // instead (segsDrawn stays false), so the frame cost is bounded
                // no matter how dense the curve family gets.
                constexpr size_t kFrameSegCap = 65536;
                const bool segBudget = segScratch_.size() / 5 < kFrameSegCap;
                if (ownTex && !t.cov->segs.empty() && segBudget)
                {
                    appendSegs(t);
                    segsDrawn = true;
                }
                else if (!ownTex && haveStandin && drawnPayload == t.standinCov->payloadId &&
                         !t.standinCov->segs.empty() && segBudget &&
                         t.standinCov->strokeAlpha >= 1.0f)
                {
                    // Ancestor stroke drafts ONLY from a solidly-sparse
                    // ancestor: near its saturation ramp the coarser grid's
                    // extraction is partially aliased, and magnifying it
                    // painted plausible but FALSE curves that morphed at every
                    // zoom step. For density the ancestor could not resolve,
                    // its band raster is the honest draft.
                    appendSegsMapped(t);
                    segsDrawn = true;
                }
            }
        }

        // The band raster ALWAYS draws -- it is the primary, zoom-consistent
        // representation (no representation switches = no seams). Where the
        // crisp stroke overlay is active, the band softens to a faint glow
        // underneath instead of vanishing.
        if (segsDrawn) inst.misc[3] = 0.30f;
        worldToNdc(vp, fbW_, fbH_, t.rect.x0, t.rect.y0, inst.ndc[0], inst.ndc[1]);
        worldToNdc(vp, fbW_, fbH_, t.rect.x1, t.rect.y1, inst.ndc[2], inst.ndc[3]);
        inst.uv[0] = u0;
        inst.uv[1] = v0;
        inst.uv[2] = u1;
        inst.uv[3] = v1;
        if (fadeArr < 0) fadeArr = ownArr; // no fade: source sampler irrelevant
        buckets_[static_cast<size_t>(ownArr) * nArr + fadeArr].push_back(inst);
    }

    // one instanced draw per non-empty (ownArray, fadeArray) bucket -- almost
    // always a single bucket; never more than nArr^2
    instUpload_.clear();
    drawnTiles_ = 0;
    for (const auto &b : buckets_) drawnTiles_ += static_cast<int>(b.size());
    if (drawnTiles_ > 0)
    {
        instUpload_.reserve(static_cast<size_t>(drawnTiles_));
        for (const auto &b : buckets_) instUpload_.insert(instUpload_.end(), b.begin(), b.end());
        glUseProgram(tileProgram_);
        glUniform1i(uTiles_, 0);
        glUniform1i(uTilesFrom_, 1);
        glUniform3f(uFill_, 0.0f, 0.55f, 0.98f);
        glBindVertexArray(quadVao_);
        glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
        const GLsizeiptr bytes = static_cast<GLsizeiptr>(instUpload_.size() * sizeof(Inst));
        glBufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_STREAM_DRAW); // orphan
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, instUpload_.data());
        size_t offset = 0;
        for (size_t bi = 0; bi < buckets_.size(); ++bi)
        {
            const size_t n = buckets_[bi].size();
            if (n == 0) continue;
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, tileArrays_[bi / nArr]);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D_ARRAY, tileArrays_[bi % nArr]);
            // GL 3.3 has no baseInstance: re-point the instance attributes at
            // this bucket's byte offset within the shared VBO instead.
            for (int loc = 1; loc <= 4; ++loc)
                glVertexAttribPointer(
                    loc, 4, GL_FLOAT, GL_FALSE, sizeof(Inst),
                    reinterpret_cast<const void *>(offset * sizeof(Inst) +
                                                   static_cast<size_t>(loc - 1) * 16));
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(n));
            offset += n;
        }
        glActiveTexture(GL_TEXTURE0);
    }

    // ---- equality-curve strokes (constant screen width, on top of the band) --
    if (!segScratch_.empty())
    {
        glUseProgram(segProgram_);
        glUniform2f(uSegPx_, fbW_ * 0.5f, fbH_ * 0.5f);
        glUniform1f(uSegHalfW_, 1.4f);
        glUniform4f(uSegColor_, 0.30f, 0.70f, 1.0f, 1.0f);
        glBindVertexArray(segVao_);
        glBindBuffer(GL_ARRAY_BUFFER, segVbo_);
        const GLsizeiptr sBytes = static_cast<GLsizeiptr>(segScratch_.size() * sizeof(float));
        glBufferData(GL_ARRAY_BUFFER, sBytes, nullptr, GL_STREAM_DRAW); // orphan
        glBufferSubData(GL_ARRAY_BUFFER, 0, sBytes, segScratch_.data());
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(segScratch_.size() / 5));
    }

    evictSlots(frame_);
    if (lastShown_.size() > 16384) lastShown_.clear();
    if (fades_.size() > 4096) fades_.clear();
    glBindVertexArray(0);
    return pendingUploads;
}
}
