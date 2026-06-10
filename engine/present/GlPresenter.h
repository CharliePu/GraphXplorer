#ifndef GXR_PRESENT_GLPRESENTER_H
#define GXR_PRESENT_GLPRESENTER_H

#include "Presenter.h"
#include "tile/TileKey.h"

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gxr
{
// OpenGL 3.3 compositor. One R32F texture per resident coverage tile, drawn as a
// world-positioned textured quad with coverage->alpha blending, plus axes/grid.
// All GL calls happen on the thread that constructs and drives it (the main
// thread). Requires a current GL context and a loaded glad before construction.
class GlPresenter : public Presenter
{
public:
    explicit GlPresenter(int tilePx);
    ~GlPresenter() override;

    GlPresenter(const GlPresenter &) = delete;
    GlPresenter &operator=(const GlPresenter &) = delete;

    void resize(int fbWidth, int fbHeight) override;
    [[nodiscard]] int renderFrame(const Viewport &vp, const std::vector<PresentTile> &tiles,
                                  int uploadBudget) override;
    // True holes drawn last frame (region with no own tile AND no resident stand-in).
    // The seamless-swap target is 0; an in-flight upload draws a stand-in, not a hole.
    [[nodiscard]] int lastHoleTiles() const { return holeTiles_; }
    // Per-frame latency attribution: time spent inside glTexImage2D uploads, how
    // many tiles were uploaded, and how many quads were drawn last frame.
    [[nodiscard]] double lastUploadMs() const { return uploadMs_; }
    [[nodiscard]] int lastUploads() const { return uploads_; }
    [[nodiscard]] int lastDrawnTiles() const { return drawnTiles_; }

private:
    struct TileTex
    {
        unsigned int id{0};
        uint64_t lastFrame{0};
        bool pooled{false}; // standard tilePx-sized: recycled through freeTex_
    };

    unsigned int compile(const char *vs, const char *fs);
    // Grow the recycled-texture pool toward `target` total tile textures, so
    // steady-state uploads recycle instead of allocating: a LIVE glTexImage2D
    // under driver backpressure was observed stalling 20-50ms; pre-allocating
    // while the GPU is quiet (startup / right after a resize) avoids that.
    void prewarmPool(size_t target);
    // Upload/refresh the texture for a coverage tile, keyed by its payload id so a
    // fallback ancestor's texture is shared by every child quad sampling it.
    // Returns the GL texture id, or 0 if not resident this frame. An already-
    // resident payload ALWAYS hits, regardless of budgets. An upload is gated by
    // the count budget + the per-frame time budget; `critical` uploads (the
    // alternative is a hole) bypass those but spend the bounded critical budget.
    unsigned int ensureTexture(const CoverageTilePtr &cov, int &budget, int &criticalLeft,
                               uint64_t frame, bool critical);
    void evictTextures(uint64_t frame);

    int tilePx_;
    int fbW_{1}, fbH_{1};
    uint64_t frame_{0};
    int holeTiles_{0}; // true holes drawn last frame (no own tile, no resident stand-in)
    double uploadMs_{0.0}; // time inside texture uploads last frame
    int uploads_{0};       // textures uploaded last frame
    int drawnTiles_{0};    // quads drawn last frame

    unsigned int tileProgram_{0};
    unsigned int lineProgram_{0};
    unsigned int quadVao_{0}, quadVbo_{0};
    unsigned int lineVao_{0}, lineVbo_{0};
    unsigned int dummyTex_{0}; // 1x1, bound for flat (textureless) draws

    int uNdcRect_{-1}, uUvRect_{-1}, uTex_{-1}, uFill_{-1}, uFlatMode_{-1}, uFlatValue_{-1};
    int uLineColor_{-1};

    std::unordered_map<uint64_t, TileTex> textures_;
    // Recycled tilePx-sized texture objects, stamped with their eviction frame.
    // A texture is only reused a few frames after eviction: glTexSubImage2D into
    // an object the GPU may still be reading forces an implicit driver sync
    // (observed as one-off 20-30ms uploads).
    std::deque<std::pair<unsigned int, uint64_t>> freeTex_;
    std::vector<unsigned char> upload8_; // R8 quantization scratch (reused)
    // Residency continuity: what each tile key last actually DREW (payload + the
    // uv it was drawn with -- world-aligned, so verbatim-reusable). While a
    // republished payload waits for upload budget, the key keeps drawing its
    // previous texture (same world region, earlier pass) -- never a downgrade
    // to an ancestor, never a hole, zero extra uploads.
    struct Shown
    {
        uint64_t payload{0};
        float u0{0}, v0{0}, u1{1}, v1{1};
    };
    std::unordered_map<TileKey, Shown> lastShown_;
};
}

#endif // GXR_PRESENT_GLPRESENTER_H
