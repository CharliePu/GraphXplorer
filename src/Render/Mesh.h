//
// Created by charl on 6/2/2024.
//

#ifndef MESH_H
#define MESH_H

#include <memory>
#include <vector>

namespace staplegl
{
    class texture_2d;
    class vertex_array;
    class shader_program;
}

enum class MeshPrimitive
{
    Triangles,
    Lines
};

struct Mesh {
    std::shared_ptr<staplegl::shader_program> shader;
    std::shared_ptr<staplegl::vertex_array> vao;
    std::vector<std::shared_ptr<staplegl::texture_2d>> textures;
    MeshPrimitive primitive{MeshPrimitive::Triangles};
    int elementCount{0};
    float lineWidth{1.0f};
    bool indexed{true};
    int vertexCount{0};
};



#endif //MESH_H
