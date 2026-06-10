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
// OpenGL 3.3 compositor. All resident tile rasters live as R8 layers of ONE
// texture array (allocated once -- uploads are glTexSubImage3D into existing
// storage, so there is no per-tile allocation churn), and every tile quad of a
// frame is drawn by a single instanced draw call with per-instance attributes
// (rect, uv+layer, crossfade source, flat fill). All GL calls happen on the
// thread that constructs and drives it. Requires a current GL context.
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
    // Per-frame latency attribution: time spent inside texture uploads, how
    // many tiles were uploaded, and how many quads were drawn last frame.
    [[nodiscard]] double lastUploadMs() const { return uploadMs_; }
    [[nodiscard]] int lastUploads() const { return uploads_; }
    [[nodiscard]] int lastDrawnTiles() const { return drawnTiles_; }
    // Crossfades animating last frame: while >0 the caller must keep rendering
    // (~8ms cadence) so refinement upgrades melt in instead of popping.
    [[nodiscard]] int activeFades() const { return fadesActive_; }
    // atlas occupancy diagnostics
    [[nodiscard]] size_t residentLayers() const { return layers_.size(); }
    [[nodiscard]] size_t freeLayers() const { return freeLayers_.size(); }

private:
    struct TileTex
    {
        int slot{-1}; // global: array index * layersPerArray_ + layer
        uint64_t lastFrame{0};
    };

    unsigned int compile(const char *vs, const char *fs);
    // Upload/refresh the array layer for a coverage tile, keyed by its payload
    // id so a fallback ancestor's layer is shared by every quad sampling it.
    // Returns the layer, or -1 if not resident this frame. A resident payload
    // ALWAYS hits regardless of budgets; an upload is rationed by the count
    // budget + the per-frame time budget, except `critical` uploads (the
    // alternative is a hole), which spend their own bounded budget.
    int ensureSlot(const CoverageTilePtr &cov, int &budget, int &criticalLeft, uint64_t frame,
                   bool critical);
    void evictSlots(uint64_t frame);

    int tilePx_;
    int fbW_{1}, fbH_{1};
    uint64_t frame_{0};
    int holeTiles_{0};
    double uploadMs_{0.0};
    int uploads_{0};
    int drawnTiles_{0};

    unsigned int tileProgram_{0};
    unsigned int lineProgram_{0};
    unsigned int quadVao_{0}, quadVbo_{0}, instVbo_{0};
    unsigned int lineVao_{0}, lineVbo_{0};
    // The tile atlas: SEVERAL R8 2D arrays (drivers commonly clamp
    // GL_MAX_ARRAY_TEXTURE_LAYERS to 2048, below a dense 4K view's working
    // set). Instances are bucketed by their (own array, fade-source array)
    // pair and drawn with one instanced call per bucket -- GL 3.3 forbids
    // non-uniform sampler indexing, buckets sidestep it. A resident slot is
    // global: array = slot / layersPerArray_, layer = slot % layersPerArray_.
    std::vector<unsigned int> tileArrays_;
    int layersPerArray_{0};
    int slotCount_{0};

    int uFill_{-1}, uTiles_{-1}, uTilesFrom_{-1};
    int uLineColor_{-1};

    // Per-instance record for the single instanced tile draw.
    struct Inst
    {
        float ndc[4];    // x0,y0,x1,y1
        float uv[4];     // u0,v0,u1,v1
        float uvFrom[4]; // crossfade source sub-rect
        float misc[4];   // x=LOCAL layer (<0 => flat), y=local layerFrom, z=fade, w=flatValue
    };
    std::vector<std::vector<Inst>> buckets_; // per (ownArray, fadeArray) pair
    std::vector<Inst> instUpload_;           // concatenated for the VBO

    std::unordered_map<uint64_t, TileTex> layers_; // payloadId -> resident layer
    // Free layers, stamped with their release frame: a layer is only reused a
    // few frames later so glTexSubImage3D never hits a still-in-flight read
    // (implicit driver sync, observed as one-off 20-30ms uploads).
    std::deque<std::pair<int, uint64_t>> freeLayers_;
    std::vector<unsigned char> upload8_; // R8 quantization scratch

    // Residency continuity: what each tile key last actually DREW (payload +
    // the uv it was drawn with). While a republished payload waits for budget,
    // the key keeps drawing its previous layer -- never a downgrade to an
    // ancestor, never a hole, zero extra uploads.
    struct Shown
    {
        uint64_t payload{0};
        float u0{0}, v0{0}, u1{1}, v1{1};
    };
    std::unordered_map<TileKey, Shown> lastShown_;

    // Active crossfades: when a key's shown payload changes (a finer ladder
    // pass, or stand-in -> own raster), the previous content fades out over
    // kFadeMs. Mid-fade retargets keep the original source and start time.
    struct Fade
    {
        uint64_t payload{0};
        float u0{0}, v0{0}, u1{1}, v1{1};
        double t0{0};
    };
    std::unordered_map<TileKey, Fade> fades_;
    int fadesActive_{0};
};
}

#endif // GXR_PRESENT_GLPRESENTER_H
