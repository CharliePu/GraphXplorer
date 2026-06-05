#ifndef GXR_PRESENT_OVERLAY_H
#define GXR_PRESENT_OVERLAY_H

#include <array>
#include <cstdint>
#include <string>

namespace gxr
{
// Minimal screen-space UI overlay: a freetype glyph atlas for text plus solid
// rectangles for panels. Screen coordinates are pixels with the origin at the
// top-left (y grows downward). All GL on the constructing (main) thread; needs a
// current GL context. One shared shader handles both textured glyphs and solid
// fills via a uniform.
class Overlay
{
public:
    Overlay(const std::string &fontPath, int pixelHeight);
    ~Overlay();
    Overlay(const Overlay &) = delete;
    Overlay &operator=(const Overlay &) = delete;

    [[nodiscard]] bool ok() const { return ok_; }
    void resize(int fbW, int fbH);

    void begin(); // sets program + blend state for a batch of overlay draws

    void fillRect(float x, float y, float w, float h, std::array<float, 4> color);
    void text(float x, float y, const std::string &s, float scale, std::array<float, 4> color);

    [[nodiscard]] float textWidth(const std::string &s, float scale) const;
    [[nodiscard]] float lineHeight(float scale) const;

private:
    struct Glyph
    {
        float ax{0};        // advance (px)
        float bw{0}, bh{0}; // bitmap size
        float bl{0}, bt{0}; // bearing left/top
        float u0{0}, v0{0}, u1{0}, v1{0}; // atlas uv
        bool valid{false};
    };

    void toNdc(float px, float py, float &nx, float &ny) const;
    void drawVerts(const float *data, int vertCount, int useTex, std::array<float, 4> color);

    bool ok_{false};
    int fbW_{1}, fbH_{1};
    float ascent_{0};
    float lineH_{0};

    unsigned int program_{0};
    unsigned int vao_{0}, vbo_{0};
    unsigned int atlas_{0};
    int uColor_{-1}, uUseTex_{-1}, uAtlas_{-1};

    std::array<Glyph, 128> glyphs_{};
};
}

#endif // GXR_PRESENT_OVERLAY_H
