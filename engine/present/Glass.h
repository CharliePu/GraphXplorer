#ifndef GXR_PRESENT_GLASS_H
#define GXR_PRESENT_GLASS_H

namespace gxr
{
// Frosted-glass UI panels (GL 3.3). Once per frame, capture() snapshots the
// rendered backbuffer into a quarter-res texture and gaussian-blurs it (two
// separable iterations -- cheap, ~0.1 ms); panel() then draws an SDF rounded
// rectangle that samples the blurred scene with a dark tint, a luminous rim,
// and a soft drop shadow. The graph stays alive and readable THROUGH the UI.
class Glass
{
public:
    Glass();
    ~Glass();
    Glass(const Glass &) = delete;
    Glass &operator=(const Glass &) = delete;

    void resize(int fbWidth, int fbHeight);
    // Snapshot + blur the current backbuffer. Call after the scene (and any
    // content that should frost under panels), before the panel() calls.
    void capture();
    // Frosted panel at top-left-origin pixel coordinates. `alpha` scales the
    // whole panel (sleeping capsules, spring-in/out transitions).
    void panel(float x, float y, float w, float h, float radius, float alpha = 1.0f);

private:
    unsigned int compile(const char *vs, const char *fs);
    void makeTargets();

    int fbW_{1}, fbH_{1}, qW_{1}, qH_{1};
    unsigned int blurProg_{0}, panelProg_{0};
    unsigned int vao_{0}, vbo_{0};
    unsigned int fbo_[2] = {0, 0};
    unsigned int tex_[2] = {0, 0};
    int uBlurTex_{-1}, uBlurDir_{-1};
    int uRect_{-1}, uRadius_{-1}, uFbSize_{-1}, uPanelTex_{-1}, uPanelAlpha_{-1};
};
}

#endif // GXR_PRESENT_GLASS_H
