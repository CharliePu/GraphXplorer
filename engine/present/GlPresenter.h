#ifndef GXR_PRESENT_GLPRESENTER_H
#define GXR_PRESENT_GLPRESENTER_H

#include "Presenter.h"
#include "tile/TileKey.h"

#include <cstdint>
#include <unordered_map>

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
    void renderFrame(const Viewport &vp, const std::vector<PresentTile> &tiles,
                     int uploadBudget) override;

private:
    struct TileTex
    {
        unsigned int id{0};
        uint64_t payload{0};
        uint64_t lastFrame{0};
    };

    unsigned int compile(const char *vs, const char *fs);
    void ensureTexture(const PresentTile &t, int &budget, uint64_t frame);
    void evictTextures(uint64_t frame);

    int tilePx_;
    int fbW_{1}, fbH_{1};
    uint64_t frame_{0};

    unsigned int tileProgram_{0};
    unsigned int lineProgram_{0};
    unsigned int quadVao_{0}, quadVbo_{0};
    unsigned int lineVao_{0}, lineVbo_{0};

    int uNdcRect_{-1}, uTex_{-1}, uFill_{-1};
    int uLineColor_{-1};

    std::unordered_map<TileKey, TileTex> textures_;
};
}

#endif // GXR_PRESENT_GLPRESENTER_H
