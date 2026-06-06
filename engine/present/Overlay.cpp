#include "Overlay.h"

#include <glad/glad.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdio>
#include <vector>

namespace gxr
{
namespace
{
const char *kVs = R"(#version 330 core
layout(location=0) in vec2 ndc;
layout(location=1) in vec2 uv;
out vec2 vUv;
void main(){ gl_Position = vec4(ndc, 0.0, 1.0); vUv = uv; })";

const char *kFs = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D atlas;
uniform vec4 color;
uniform int useTex;
void main(){
    float a = (useTex == 1) ? texture(atlas, vUv).r : 1.0;
    if (a <= 0.001) discard;
    frag = vec4(color.rgb, color.a * a);
})";

unsigned int compile(const char *vs, const char *fs)
{
    auto mk = [](GLenum t, const char *s) {
        GLuint sh = glCreateShader(t);
        glShaderSource(sh, 1, &s, nullptr);
        glCompileShader(sh);
        GLint ok = 0;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[512];
            glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
            std::fprintf(stderr, "overlay shader error: %s\n", log);
        }
        return sh;
    };
    GLuint v = mk(GL_VERTEX_SHADER, vs), f = mk(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}
}

Overlay::Overlay(const std::string &fontPath, int pixelHeight)
{
    FT_Library ft;
    if (FT_Init_FreeType(&ft) != 0)
    {
        std::fprintf(stderr, "overlay: FT_Init failed\n");
        return;
    }
    FT_Face face;
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face) != 0)
    {
        std::fprintf(stderr, "overlay: cannot open font '%s'\n", fontPath.c_str());
        FT_Done_FreeType(ft);
        return;
    }
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelHeight));
    ascent_ = static_cast<float>(face->size->metrics.ascender >> 6);
    lineH_ = static_cast<float>(face->size->metrics.height >> 6);

    // first pass: metrics + atlas dimensions (single row)
    int atlasW = 0, atlasH = 0;
    for (int c = 32; c < 127; ++c)
    {
        if (FT_Load_Char(face, static_cast<FT_ULong>(c), FT_LOAD_RENDER) != 0) continue;
        const FT_GlyphSlot g = face->glyph;
        atlasW += static_cast<int>(g->bitmap.width) + 1;
        atlasH = std::max(atlasH, static_cast<int>(g->bitmap.rows));
    }
    if (atlasW <= 0 || atlasH <= 0)
    {
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return;
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(atlasW) * atlasH, 0);
    int penX = 0;
    for (int c = 32; c < 127; ++c)
    {
        if (FT_Load_Char(face, static_cast<FT_ULong>(c), FT_LOAD_RENDER) != 0) continue;
        const FT_GlyphSlot g = face->glyph;
        const int w = static_cast<int>(g->bitmap.width);
        const int h = static_cast<int>(g->bitmap.rows);
        for (int row = 0; row < h; ++row)
            for (int col = 0; col < w; ++col)
                buffer[static_cast<size_t>(row) * atlasW + penX + col] =
                    g->bitmap.buffer[static_cast<size_t>(row) * g->bitmap.pitch + col];

        Glyph &gl = glyphs_[static_cast<size_t>(c)];
        gl.valid = true;
        gl.ax = static_cast<float>(g->advance.x >> 6);
        gl.bw = static_cast<float>(w);
        gl.bh = static_cast<float>(h);
        gl.bl = static_cast<float>(g->bitmap_left);
        gl.bt = static_cast<float>(g->bitmap_top);
        gl.u0 = static_cast<float>(penX) / atlasW;
        gl.v0 = 0.0f;
        gl.u1 = static_cast<float>(penX + w) / atlasW;
        gl.v1 = static_cast<float>(h) / atlasH;
        penX += w + 1;
    }
    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    // upload atlas (single channel)
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &atlas_);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasW, atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, buffer.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    program_ = compile(kVs, kFs);
    uColor_ = glGetUniformLocation(program_, "color");
    uUseTex_ = glGetUniformLocation(program_, "useTex");
    uAtlas_ = glGetUniformLocation(program_, "atlas");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void *>(2 * sizeof(float)));
    glBindVertexArray(0);
    ok_ = true;
}

Overlay::~Overlay()
{
    if (atlas_) glDeleteTextures(1, &atlas_);
    if (program_) glDeleteProgram(program_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
}

void Overlay::resize(int fbW, int fbH)
{
    fbW_ = std::max(1, fbW);
    fbH_ = std::max(1, fbH);
}

void Overlay::toNdc(float px, float py, float &nx, float &ny) const
{
    nx = px / fbW_ * 2.0f - 1.0f;
    ny = 1.0f - py / fbH_ * 2.0f; // screen y is top-down, NDC y is up
}

void Overlay::begin()
{
    if (!ok_) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glUniform1i(uAtlas_, 0);
    glBindVertexArray(vao_);
}

void Overlay::drawVerts(const float *data, int vertCount, int useTex, std::array<float, 4> color)
{
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertCount) * 4 * sizeof(float), data,
                 GL_DYNAMIC_DRAW);
    glUniform4f(uColor_, color[0], color[1], color[2], color[3]);
    glUniform1i(uUseTex_, useTex);
    glDrawArrays(GL_TRIANGLES, 0, vertCount);
}

void Overlay::fillRect(float x, float y, float w, float h, std::array<float, 4> color)
{
    if (!ok_) return;
    float a, b, c, d;
    toNdc(x, y, a, b);
    toNdc(x + w, y + h, c, d);
    const float verts[] = {a, b, 0, 0, c, b, 0, 0, c, d, 0, 0,
                           a, b, 0, 0, c, d, 0, 0, a, d, 0, 0};
    drawVerts(verts, 6, 0, color);
}

void Overlay::rectOutline(float x, float y, float w, float h, float t, std::array<float, 4> color)
{
    if (!ok_) return;
    std::vector<float> verts;
    verts.reserve(24 * 4);
    auto pushRect = [&](float rx, float ry, float rw, float rh) {
        float a, b, c, d;
        toNdc(rx, ry, a, b);
        toNdc(rx + rw, ry + rh, c, d);
        const float q[] = {a, b, 0, 0, c, b, 0, 0, c, d, 0, 0, a, b, 0, 0, c, d, 0, 0, a, d, 0, 0};
        verts.insert(verts.end(), std::begin(q), std::end(q));
    };
    pushRect(x, y, w, t);         // top
    pushRect(x, y + h - t, w, t); // bottom
    pushRect(x, y, t, h);         // left
    pushRect(x + w - t, y, t, h); // right
    drawVerts(verts.data(), static_cast<int>(verts.size() / 4), 0, color);
}

void Overlay::text(float x, float y, const std::string &s, float scale, std::array<float, 4> color)
{
    if (!ok_) return;
    const float baseline = y + ascent_ * scale;
    std::vector<float> verts;
    verts.reserve(s.size() * 24);
    float penX = x;
    for (unsigned char ch : s)
    {
        if (ch >= glyphs_.size() || !glyphs_[ch].valid)
        {
            penX += lineH_ * 0.5f * scale;
            continue;
        }
        const Glyph &g = glyphs_[ch];
        const float x0 = penX + g.bl * scale;
        const float y0 = baseline - g.bt * scale;
        const float x1 = x0 + g.bw * scale;
        const float y1 = y0 + g.bh * scale;
        float a, b, c, d;
        toNdc(x0, y0, a, b);
        toNdc(x1, y1, c, d);
        const float quad[] = {a, b, g.u0, g.v0, c, b, g.u1, g.v0, c, d, g.u1, g.v1,
                              a, b, g.u0, g.v0, c, d, g.u1, g.v1, a, d, g.u0, g.v1};
        verts.insert(verts.end(), std::begin(quad), std::end(quad));
        penX += g.ax * scale;
    }
    if (!verts.empty()) drawVerts(verts.data(), static_cast<int>(verts.size() / 4), 1, color);
}

float Overlay::textWidth(const std::string &s, float scale) const
{
    float w = 0;
    for (unsigned char ch : s)
    {
        if (ch < glyphs_.size() && glyphs_[ch].valid) w += glyphs_[ch].ax * scale;
        else w += lineH_ * 0.5f * scale;
    }
    return w;
}

float Overlay::lineHeight(float scale) const { return lineH_ * scale; }
}
