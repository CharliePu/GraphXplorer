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

struct Mesh {
    std::shared_ptr<staplegl::shader_program> shader;
    std::shared_ptr<staplegl::vertex_array> vao;
    std::vector<std::shared_ptr<staplegl::texture_2d>> textures;
};



#endif //MESH_H
