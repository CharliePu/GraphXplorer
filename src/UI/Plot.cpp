//
// Created by charl on 6/2/2024.
//

#include "Plot.h"

#include <glad/glad.h>
#include <bits/ranges_algo.h>
#include <staplegl/staplegl.hpp>

#include "../Math/Formula.h"
#include "../Math/Graph.h"
#include "../Math/ComputeEngine.h"
#include "../Render/GraphRasterizer.h"
#include "../Render/Mesh.h"
#include "../Core/Window.h"

Plot::Plot(const std::shared_ptr<ComputeEngine> &engine, const std::shared_ptr<Window> &window): graph{},
    formula{},
    computeEngine{engine},
    graphRasterizer{std::make_shared<GraphRasterizer>(window)},
    window{window},
    yRange{-10.0, 10.0},
    xRange{-10.0, 10.0},
    vao{std::make_shared<staplegl::vertex_array>()},
    shader{
        new staplegl::shader_program{
            "plot_shader",
            {
                std::pair{staplegl::shader_type::vertex, "../shader/plot.vert"},
                std::pair{staplegl::shader_type::fragment, "../shader/plot.frag"}
            }
        }
    }
{
    computeEngine->setComputeCompleteCallback([this]() {
        graphRasterizer->rasterize(graph, xRange, yRange);
    });

    graphRasterizer->setRasterizeCompleteCallback([this](const std::vector<Interval<bool> > &image) {
        const auto meshes = prepareMeshes(image);
        plotCompleteCallback(meshes);
    });

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

void Plot::setPlotCompleteCallback(const std::function<void(const std::vector<Mesh> &)> &callback)
{
    plotCompleteCallback = callback;
}

int Plot::getDepth() const
{
    return 10;
}

void Plot::requestNewPlot(const std::string &input)
{
    formula = std::make_shared<Formula>(input);

    graph = std::make_shared<Graph>();

    computeEngine->run(graph, {formula, xRange, yRange});
}

std::vector<Mesh> Plot::prepareMeshes(const std::vector<Interval<bool> > &image)
{
    auto getGradent = [](const Interval<bool> &interval) -> float {
        return static_cast<float>(interval.lower * 0.5 + interval.upper * 0.5);
    };

    std::vector<float> data;

    std::ranges::transform(image, std::back_inserter(data), getGradent);

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
