//
// Created by charl on 6/2/2024.
//

#include "Plot.h"

#include <glad/glad.h>
#include <algorithm>
#include <staplegl/staplegl.hpp>

#include "../Math/Formula.h"
#include "../Math/Graph.h"
#include "../Math/GraphProcessor.h"
#include "../Math/GraphRasterizer.h"
#include "../Render/Mesh.h"
#include "../Core/Window.h"


void Plot::prepareVertices() const
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

Plot::Plot(const std::shared_ptr<GraphProcessor> &processor, const std::shared_ptr<GraphRasterizer> &rasterizer,
           const std::shared_ptr<Window> &window): graph{},
                                                   formula{},
                                                   computeEngine{processor},
                                                   graphRasterizer{rasterizer},
                                                   window{window},
                                                   xRange{-20.0, 20.0},
                                                   yRange{-20.0, 20.0},
                                                   vao{std::make_shared<staplegl::vertex_array>()},
                                                   shader{
                                                       new staplegl::shader_program{
                                                           "plot_shader",
                                                           {
                                                               std::pair{
                                                                   staplegl::shader_type::vertex, "./shader/plot.vert"
                                                               },
                                                               std::pair{
                                                                   staplegl::shader_type::fragment, "./shader/plot.frag"
                                                               }
                                                           }
                                                       }
                                                   },
                                                   model{1.0}
{
    computeEngine->setComputeCompleteCallback([this](const ComputeRequest &result) {
        graphRasterizer->rasterizeTemp(result.graph, result.xRange, result.yRange, result.windowWidth, result.windowHeight);
    });

    graphRasterizer->setRasterizeCompleteCallback([this](const std::vector<int> &image) {
        // Reset temporary transformation
        model = glm::mat4{1.0};
        const auto meshes = prepareMeshes(image);

        plotCompleteCallback(meshes);
    });

    prepareVertices();
}

void Plot::setPlotCompleteCallback(const std::function<void(const std::vector<Mesh> &)> &callback)
{
    plotCompleteCallback = callback;
}

void Plot::setPlotRangeChangedCallback(
    const std::function<void(const Interval<double> &, const Interval<double> &)> &callback)
{
    plotRangeChangedCallback = callback;
}

int Plot::getDepth() const
{
    return 2;
}

void Plot::requestNewPlot(const std::string &input)
{
    formula = std::make_shared<Formula>(input);

    graph = std::make_shared<Graph>();

    computeEngine->requestProcessGraph({graph, formula, xRange, yRange, window->getWidth(), window->getHeight()});
}

std::vector<Mesh> Plot::prepareMeshes(const std::vector<int> &image)
{
    auto getGradent = [](const int value) -> float {
        return static_cast<float>(value > 0);
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

    shader->bind();
    shader->upload_uniform_mat4f("transform", std::span<float, 16>{glm::value_ptr(model), 16});

    plotMesh = {shader, vao, std::vector{texture}};

    return {plotMesh};
}

void Plot::onCursorDrag(const double x, const double y)
{
    const auto windowWidth{window->getWidth()};
    const auto windowHeight{window->getHeight()};

    const auto deltaX{xRange.size() / windowWidth};
    const auto deltaY{yRange.size() / windowHeight};

    xRange = xRange + x * -deltaX;
    yRange = yRange + y * deltaY;

    plotRangeChangedCallback(xRange, yRange);

    // Temporarily offset the old graph
    // Once the new graph is ready, the transformation will be reset
    model = glm::translate(model, glm::vec3{x * deltaX / xRange.size() * 2.0, y * -deltaY / xRange.size() * 2.0, 0.0f});
    // TODO: make plotMesh a pointer and check if it is nullptr
    if (plotMesh.shader != nullptr)
    {
        plotMesh.shader->bind();
        plotMesh.shader->upload_uniform_mat4f("transform", std::span<float, 16>{glm::value_ptr(model), 16});
        plotCompleteCallback({plotMesh});
    }

    if (formula)
    {
        computeEngine->requestProcessGraph({graph, formula, xRange, yRange, windowWidth, windowHeight});
    }
}

void Plot::onWindowSizeChanged(const int width, const int height)
{
    auto ratio = width / static_cast<double>(height);

    double xRangeSize = yRange.size() * ratio;

    double xRangeMid = (xRange.lower + xRange.upper) / 2.0;

    xRange = {xRangeMid - xRangeSize / 2.0, xRangeMid + xRangeSize / 2.0};

    computeEngine->requestProcessGraph({graph, formula, xRange, yRange, width, height});
}

Interval<double> Plot::getXRanges() const
{
    return xRange;
}

Interval<double> Plot::getYRanges() const
{
    return yRange;
}
