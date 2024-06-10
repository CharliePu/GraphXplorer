//
// Created by charl on 6/5/2024.
//

#include "Grid.h"

#include <staplegl/staplegl.hpp>
#include <cmath>
#include "../Render/Mesh.h"
#include "../Core/Window.h"

Grid::Grid(const std::shared_ptr<Window> &window): window{window},
                                                   vao{std::make_shared<staplegl::vertex_array>()},
                                                   shader{
                                                       new staplegl::shader_program{
                                                           "grid_shader",
                                                           {
                                                               std::pair{
                                                                   staplegl::shader_type::vertex, "../shader/grid.vert"
                                                               },
                                                               std::pair{
                                                                   staplegl::shader_type::fragment,
                                                                   "../shader/grid.frag"
                                                               }
                                                           }
                                                       }
                                                   }
{
    prepareVertices();
}

void Grid::prepareVertices() const
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

    vao->add_vertex_buffer(std::move(vbo));
    vao->set_index_buffer(std::move(ebo));
}

// TODO: calculate grid lines in terms of pixels rather than range values to improve display precision
void Grid::updatePosition(Interval<double> xInterval, Interval<double> yInterval)
{
    auto positiveModulo = [](double a, double b) -> double {
        return std::fmod(std::fmod(a, b) + b, b);
    };

    std::vector<float> data;

    const auto width = window->getWidth();
    const auto height = window->getHeight();

    constexpr auto axisLineWidth{2.0}, majorLineWidth{2.0}, minorLineWidth{1.0};

    const auto xMajorGrid{std::pow(10.0, std::floor(std::log10(xInterval.size())))};
    const auto yMajorGrid{std::pow(10.0, std::floor(std::log10(yInterval.size())))};
    const auto xMinorGrid{xMajorGrid / 5.0};
    const auto yMinorGrid{yMajorGrid / 5.0};

    for (auto j = 0; j < height; ++j)
    {
        const auto y = yInterval.lower + j * yInterval.size() / height;
        for (auto i = 0; i < width; ++i)
        {
            if (const auto x = xInterval.lower + i * xInterval.size() / width;
                std::abs(x) < axisLineWidth / width * xInterval.size() || std::abs(y) < axisLineWidth / height *
                yInterval.size())
            {
                data.push_back(1.0f);
            }
            else if (positiveModulo(x, xMajorGrid) < majorLineWidth / width * xInterval.size() ||
                     positiveModulo(y, yMajorGrid) < majorLineWidth / height * yInterval.size())
            {
                data.push_back(0.8f);
            }
            else if (positiveModulo(x, xMinorGrid) < minorLineWidth / width * xInterval.size() ||
                     positiveModulo(y, yMinorGrid) < minorLineWidth / height * yInterval.size())
            {
                data.push_back(0.5f);
            }
            else
            {
                data.push_back(0.0f);
            }
        }
    }

    updatePositionCallback(prepareMeshes(data));
}

std::vector<Mesh> Grid::prepareMeshes(const std::vector<float> &data)
{
    const auto textureData = std::span<const float>{data};
    const auto textureResolution = staplegl::resolution{window->getWidth(), window->getHeight()};
    constexpr auto textureColor = staplegl::texture_color{
        GL_RED, GL_RED, GL_FLOAT
    };
    constexpr auto textureFilter = staplegl::texture_filter{
        GL_NEAREST, GL_NEAREST
    };

    auto texture = std::make_shared<staplegl::texture_2d>(
        textureData,
        textureResolution,
        textureColor,
        textureFilter
    );

    return {{shader, vao, std::vector{texture}}};
}

void Grid::setUpdatePositionCallback(const std::function<void(const std::vector<Mesh> &)> &callback)
{
    updatePositionCallback = callback;
}

int Grid::getDepth() const
{
    return 5;
}