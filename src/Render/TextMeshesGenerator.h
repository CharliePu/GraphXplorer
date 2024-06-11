//
// Created by charl on 6/9/2024.
//

#ifndef TEXTMESHESGENERATOR_H
#define TEXTMESHESGENERATOR_H
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <freetype/freetype.h>
#include <glad/glad.h>

#include "../UI/Plot.h"

#include FT_FREETYPE_H

struct Mesh;

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
    TextMeshesGenerator();

    ~TextMeshesGenerator();

    void loadFont(const std::string &fontPath, unsigned int fontSize);

    FT_Library ft;
    FT_Face face;
    std::map<GLchar, FontCharacter> characterMap;
    std::shared_ptr<staplegl::shader_program> shader;
};


#endif //TEXTMESHESGENERATOR_H
