#ifndef GXR_PRESENT_PRESENTER_H
#define GXR_PRESENT_PRESENTER_H

#include "app/Engine.h"
#include "tile/Geometry.h"

#include <vector>

namespace gxr
{
// Per-slot fill colors, shared by the compositor and the UI's formula-list
// swatches so they always agree. Indexed by PresentTile.slot (mod 8).
inline constexpr float kRelationPalette[8][3] = {
    {0.00f, 0.55f, 0.98f}, // blue
    {1.00f, 0.62f, 0.18f}, // amber
    {0.30f, 0.85f, 0.45f}, // green
    {0.95f, 0.35f, 0.75f}, // magenta
    {0.25f, 0.85f, 0.90f}, // cyan
    {0.95f, 0.40f, 0.30f}, // red
    {0.75f, 0.65f, 1.00f}, // lavender
    {0.95f, 0.90f, 0.40f}, // yellow
};

// Abstraction over the GPU presentation backend. This is the Vulkan-ready seam:
// the engine and app speak only this interface, so a VkPresenter can replace the
// GlPresenter without touching any compute code. The contract is deliberately
// thin because compositing cached coverage tiles is cheap on any API; all heavy
// lifting is CPU-side in the engine.
class Presenter
{
public:
    virtual ~Presenter() = default;

    // Called when the framebuffer size changes.
    virtual void resize(int fbWidth, int fbHeight) = 0;

    // Render one frame: clear, composite the given visible tiles for `vp`, draw
    // axes/grid. `uploadBudget` caps how many tile textures may be (re)uploaded
    // this frame so the main thread never hitches on transfers. Returns the number
    // of tiles that still need uploading but were skipped this frame (budget
    // exhausted) -- the caller must keep rendering until this reaches 0, else the
    // image is not actually complete on screen even though every tile is solved.
    [[nodiscard]] virtual int renderFrame(const Viewport &vp, const std::vector<PresentTile> &tiles,
                                          int uploadBudget) = 0;
};
}

#endif // GXR_PRESENT_PRESENTER_H
