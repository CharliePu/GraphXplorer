//
// Created by charl on 6/9/2024.
// Implementation adapted from https://learnopengl.com/In-Practice/Text-Rendering
//

#include "TextMeshesGenerator.h"

#include <stdexcept>
#include <staplegl/staplegl.hpp>

#include "Mesh.h"

struct FontCharacter
{
    std::shared_ptr<staplegl::texture_2d> texture;
    int width, height; // Size of glyph
    int bearingX, bearingY; // Offset from baseline to left/top of glyph
    int advance; // Horizontal offset to advance to next glyph
};

std::vector<Mesh> TextMeshesGenerator::generateTextMesh(const std::string &text, double x, double y, double scale,
                                                        TextAlign align, double aspectRatio)
{
    std::vector<Mesh> meshes;

    // Calculate the width of the text
    double textWidth{0.0};
    for (const char &c: text)
    {
        if (characterMap.find(c) == characterMap.end())
        {
            throw std::runtime_error("Character not found in font");
        }
        auto ch = characterMap[c];
        textWidth += ch.advance / 64.0 * scale / aspectRatio;
    }

    // Alignment adjustment
    if (align == TextAlign::CENTER)
    {
        x -= textWidth / 2.0f;
    }
    else if (align == TextAlign::RIGHT)
    {
        x -= textWidth;
    }

    std::shared_ptr<staplegl::shader_program> shader{
        new staplegl::shader_program{
            "grid_shader",
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
    };

    for (const char &c: text)
    {
        auto result = characterMap.find(c);
        if (result == characterMap.end())
        {
            continue;
        }

        FontCharacter ch{result->second};
        float xpos{static_cast<float>(x + ch.bearingX * scale)};
        float ypos{static_cast<float>(y - (ch.height - ch.bearingY) * scale)};

        float w{static_cast<float>(ch.width * scale / aspectRatio)};
        float h{static_cast<float>(ch.height * scale)};

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

        x += ch.advance / 64 * scale / aspectRatio;

        meshes.push_back(Mesh{shader, vao, {ch.texture}});
    }

    return meshes;
}

TextMeshesGenerator::TextMeshesGenerator(): ft{},
                                            face{}
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

