//
// Created by charl on 6/5/2024.
//

#include "Grid.h"

#include <staplegl/staplegl.hpp>
#include <cmath>
#include "../Render/Mesh.h"
#include "../Core/Window.h"

Grid::Grid(const std::shared_ptr<Window> &window): window{window}, mesh{}
{
    prepareMesh();
}

void Grid::prepareMesh()
{
    std::array vertices = {
        1.0f, 1.0f, 0.0f, // top right
        1.0f, -1.0f, 0.0f, // bottom right
        -1.0f, -1.0f, 0.0f, // bottom left
        -1.0f, 1.0f, 0.0f // top left
    };

    std::array<unsigned int, 6> indices = {
        0, 1, 3, // first Triangle
        1, 2, 3 // second Triangle
    };

    staplegl::vertex_buffer vbo{vertices, staplegl::driver_draw_hint::STATIC_DRAW};
    staplegl::index_buffer ebo{indices};

    staplegl::vertex_buffer_layout const layout{{staplegl::shader_data_type::u_type::vec3, "aPos"}};

    vbo.set_layout(layout);


    auto vao{std::make_shared<staplegl::vertex_array>()};

    std::shared_ptr<staplegl::shader_program> shader{
        new staplegl::shader_program{
            "grid_shader",
            {
                std::pair{
                    staplegl::shader_type::vertex, "./shader/grid.vert"
                },
                std::pair{
                    staplegl::shader_type::fragment,
                    "./shader/grid.frag"
                }
            }
        }
    };

    vao->bind();
    vao->add_vertex_buffer(std::move(vbo));
    vao->set_index_buffer(std::move(ebo));

    mesh = {shader, vao, std::vector<std::shared_ptr<staplegl::texture_2d> >{}};
}

void Grid::updatePosition(Interval<double> xInterval, Interval<double> yInterval)
{
    const auto xMajorGrid{std::pow(10.0, std::floor(std::log10(xInterval.size())))};
    const auto yMajorGrid{std::pow(10.0, std::floor(std::log10(yInterval.size())))};
    const auto xMinorGrid{xMajorGrid / 5.0};
    const auto yMinorGrid{yMajorGrid / 5.0};

    mesh.shader->bind();
    mesh.shader->upload_uniform2f("xRange", xInterval.lower, xInterval.upper);
    mesh.shader->upload_uniform2f("yRange", yInterval.lower, yInterval.upper);
    mesh.shader->upload_uniform2f("grid1", xMajorGrid, yMajorGrid);
    mesh.shader->upload_uniform2f("grid2", xMinorGrid, yMinorGrid);

    updatePositionCallback({mesh});
}

void Grid::setUpdatePositionCallback(const std::function<void(const std::vector<Mesh> &)> &callback)
{
    updatePositionCallback = callback;
}

int Grid::getDepth() const
{
    return 5;
}
