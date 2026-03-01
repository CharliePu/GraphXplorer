//
// Created by charl on 6/9/2024.
// Implementation adapted from https://learnopengl.com/In-Practice/Text-Rendering
//

#include "TextMeshesGenerator.h"

#include <cmath>
#include <stdexcept>
#include <staplegl/staplegl.hpp>

#include "Mesh.h"
#include "../Util/PerformanceProfiler.h"

struct FontCharacter
{
    std::shared_ptr<staplegl::texture_2d> texture;
    int width, height; // Size of glyph
    int bearingX, bearingY; // Offset from baseline to left/top of glyph
    int advance; // Horizontal offset to advance to next glyph
};

size_t TextMeshesGenerator::GlyphGeometryKeyHasher::operator()(const GlyphGeometryKey &key) const noexcept
{
    const auto h1 = std::hash<int>{}(static_cast<int>(key.glyph));
    const auto h2 = std::hash<int>{}(key.scaleMicro);
    const auto h3 = std::hash<int>{}(key.aspectMicro);
    return h1 ^ (h2 << 1) ^ (h3 << 2);
}

int TextMeshesGenerator::quantizeCacheValue(const double value)
{
    return static_cast<int>(std::llround(value * 1'000'000.0));
}

TextMeshesGenerator::GlyphGeometryKey TextMeshesGenerator::buildGlyphCacheKey(
    const GLchar glyph, const double scale, const double aspectRatio) const
{
    return GlyphGeometryKey{
        glyph,
        quantizeCacheValue(scale),
        quantizeCacheValue(aspectRatio)
    };
}

Mesh TextMeshesGenerator::buildGlyphGeometry(const GLchar glyph, const double scale, const double aspectRatio)
{
    GRAPHX_PROFILE_SCOPE("text.generateTextMesh.meshBuild");

    const auto result = characterMap.find(glyph);
    if (result == characterMap.end())
    {
        throw std::runtime_error("Character not found in font");
    }

    const FontCharacter ch{result->second};
    const float xpos{static_cast<float>(ch.bearingX * scale)};
    const float ypos{static_cast<float>(-(ch.height - ch.bearingY) * scale)};
    const float w{static_cast<float>(ch.width * scale / aspectRatio)};
    const float h{static_cast<float>(ch.height * scale)};

    auto vao{std::make_shared<staplegl::vertex_array>()};

    std::array vertices{
        xpos, ypos + h, 0.0f, 0.0f,
        xpos, ypos, 0.0f, 1.0f,
        xpos + w, ypos, 1.0f, 1.0f,
        xpos + w, ypos + h, 1.0f, 0.0f
    };

    std::array<unsigned int, 6> indices{
        0, 1, 2,
        0, 2, 3
    };

    staplegl::vertex_buffer vbo{vertices, staplegl::driver_draw_hint::STATIC_DRAW};
    staplegl::index_buffer ebo{indices};

    staplegl::vertex_buffer_layout const layout{
        {staplegl::shader_data_type::u_type::vec2, "aPos"},
        {staplegl::shader_data_type::u_type::vec2, "aTexCoord"}
    };

    vbo.set_layout(layout);

    vao->add_vertex_buffer(std::move(vbo));
    vao->set_index_buffer(std::move(ebo));

    return Mesh{shader, vao, {ch.texture}, MeshPrimitive::Triangles, static_cast<int>(indices.size())};
}

std::array<float, 16> TextMeshesGenerator::buildTranslationTransform(const double x, const double y) const
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        static_cast<float>(x), static_cast<float>(y), 0.0f, 1.0f
    };
}

std::vector<Mesh> TextMeshesGenerator::generateTextMesh(const std::string &text, double x, double y, double scale,
                                                        TextAlign align, double aspectRatio)
{
    GRAPHX_PROFILE_SCOPE("text.generateTextMesh");
    if (text.empty())
    {
        return {};
    }

    // Calculate the width of the text for alignment in local text space.
    double textWidth{0.0};
    {
        GRAPHX_PROFILE_SCOPE("text.generateTextMesh.widthPass");
        for (const char c: text)
        {
            const auto result = characterMap.find(c);
            if (result == characterMap.end())
            {
                throw std::runtime_error("Character not found in font");
            }
            textWidth += result->second.advance / 64.0 * scale / aspectRatio;
        }
    }

    double penX{0.0};
    if (align == TextAlign::CENTER)
    {
        penX -= textWidth / 2.0;
    }
    else if (align == TextAlign::RIGHT)
    {
        penX -= textWidth;
    }

    std::vector<Mesh> meshes;
    meshes.reserve(text.size());
    {
        GRAPHX_PROFILE_SCOPE("text.generateTextMesh.cacheLookup");
        for (const char c : text)
        {
            const auto result = characterMap.find(c);
            if (result == characterMap.end())
            {
                continue;
            }

            const auto key = buildGlyphCacheKey(c, scale, aspectRatio);
            auto cachedGlyph = glyphGeometryCache.find(key);
            if (cachedGlyph == glyphGeometryCache.end())
            {
                GRAPHX_PROFILE_SCOPE("text.generateTextMesh.cacheMissBuild");
                cachedGlyph = glyphGeometryCache.emplace(key, buildGlyphGeometry(c, scale, aspectRatio)).first;
            }

            auto mesh = cachedGlyph->second;
            mesh.hasMeshTransform = true;
            mesh.meshTransform = buildTranslationTransform(x + penX, y);
            meshes.push_back(std::move(mesh));

            penX += result->second.advance / 64.0 * scale / aspectRatio;
        }
    }

    return meshes;
}

TextMeshesGenerator::TextMeshesGenerator(): ft{},
                                            face{}, shader{
                                                new staplegl::shader_program{
                                                    "text_shader",
                                                    {
                                                        std::pair{
                                                            staplegl::shader_type::vertex, "./shader/text.vert"
                                                        },
                                                        std::pair{
                                                            staplegl::shader_type::fragment,
                                                            "./shader/text.frag"
                                                        }
                                                    }
                                                }
                                            }
{
    if (FT_Init_FreeType(&ft))
    {
        throw std::runtime_error("Could not init FreeType Library");
    }

    loadFont("./font/FiraCode-Regular.ttf", 48);
}

TextMeshesGenerator::~TextMeshesGenerator()
{
    FT_Done_Face(face);
    FT_Done_FreeType(ft);
}

void TextMeshesGenerator::loadFont(const std::string &fontPath, unsigned int fontSize)
{
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face))
    {
        throw std::runtime_error("Failed to load font");
    }

    FT_Set_Pixel_Sizes(face, 0, fontSize);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // Disable byte-alignment restriction

    for (unsigned char c = 0; c < 128; c++)
    {
        // Load character glyph
        if (FT_Load_Char(face, c, FT_LOAD_RENDER))
        {
            throw std::runtime_error("Failed to load Glyph");
        }

        // Generate texture
        auto textureData = std::span<const unsigned char>{
            face->glyph->bitmap.buffer, static_cast<size_t>(face->glyph->bitmap.width * face->glyph->bitmap.rows)
        };

        auto textureResolution = staplegl::resolution{
            static_cast<int>(face->glyph->bitmap.width), static_cast<int>(face->glyph->bitmap.rows)
        };

        auto textureColor = staplegl::texture_color{
            GL_RED, GL_RED, GL_UNSIGNED_BYTE
        };

        auto textureFilter = staplegl::texture_filter{
            GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE
        };

        auto texture = std::make_shared<staplegl::texture_2d>(
            textureData,
            textureResolution,
            textureColor,
            textureFilter
        );

        // Now store character for later use
        FontCharacter character = {
            texture,
            static_cast<int>(face->glyph->bitmap.width),
            static_cast<int>(face->glyph->bitmap.rows),
            face->glyph->bitmap_left,
            face->glyph->bitmap_top,
            static_cast<int>(face->glyph->advance.x)
        };

        characterMap.insert(std::pair<GLchar, FontCharacter>(c, character));
    }
}

