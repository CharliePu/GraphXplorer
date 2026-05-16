//
// Created by charl on 6/9/2024.
//

#ifndef TEXTMESHESGENERATOR_H
#define TEXTMESHESGENERATOR_H
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <freetype/freetype.h>
#include <glad/glad.h>

#include FT_FREETYPE_H

struct Mesh;

namespace staplegl
{
class shader_program;
}

enum class TextAlign
{
    LEFT,
    CENTER,
    RIGHT
};

struct FontCharacter;

class TextMeshesGenerator
{
public:
    static TextMeshesGenerator &getInstance()
    {
        static TextMeshesGenerator instance;
        return instance;
    }


    TextMeshesGenerator(const TextMeshesGenerator &) = delete;

    TextMeshesGenerator &operator=(const TextMeshesGenerator &) = delete;

    TextMeshesGenerator(TextMeshesGenerator &&) = delete;

    TextMeshesGenerator &operator=(TextMeshesGenerator &&) = delete;

    std::vector<Mesh> generateTextMesh(const std::string &text, double x, double y, double scale, TextAlign align,
                                       double aspectRatio);

private:
    struct GlyphGeometryKey
    {
        GLchar glyph{0};
        int scaleMicro{0};
        int aspectMicro{0};

        bool operator==(const GlyphGeometryKey &other) const = default;
    };

    struct GlyphGeometryKeyHasher
    {
        size_t operator()(const GlyphGeometryKey &key) const noexcept;
    };

    TextMeshesGenerator();

    ~TextMeshesGenerator();

    static int quantizeCacheValue(double value);
    GlyphGeometryKey buildGlyphCacheKey(GLchar glyph, double scale, double aspectRatio) const;
    Mesh buildGlyphGeometry(GLchar glyph, double scale, double aspectRatio);
    std::array<float, 16> buildTranslationTransform(double x, double y) const;

    void loadFont(const std::string &fontPath, unsigned int fontSize);

    FT_Library ft;
    FT_Face face;
    std::map<GLchar, FontCharacter> characterMap;
    std::unordered_map<GlyphGeometryKey, Mesh, GlyphGeometryKeyHasher> glyphGeometryCache;
    std::shared_ptr<staplegl::shader_program> shader;
};


#endif //TEXTMESHESGENERATOR_H
