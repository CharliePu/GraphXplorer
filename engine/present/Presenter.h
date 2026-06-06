#ifndef GXR_PRESENT_PRESENTER_H
#define GXR_PRESENT_PRESENTER_H

#include "app/Engine.h"
#include "tile/Geometry.h"

#include <vector>

namespace gxr
{
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
