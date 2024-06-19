//
// Created by charl on 6/2/2024.
//

#include "Plot.h"

#include <glad/glad.h>
#include <algorithm>
#include <staplegl/staplegl.hpp>

#include "../Math/Formula.h"
#include "../Graph/Graph.h"
#include "../Math/ComputeEngine.h"
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

Plot::Plot(const std::shared_ptr<ComputeEngine> &engine,
           const std::shared_ptr<Window> &window): graph{},
                                                   formula{},
                                                   computeEngine{engine},
                                                   window{window},
                                                   viewXRange{-20.0, 20.0},
                                                   viewYRange{-20.0, 20.0},
                                                   plotXRange{-20.0, 20.0},
                                                   plotYRange{-20.0, 20.0},
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
    computeEngine->setComputeCompleteCallback(
        [this](const std::vector<int> &image, Interval<double> xRange, Interval<double> yRange, int width, int height) {
            plotXRange = xRange;
            plotYRange = yRange;
            updateModelMat();

            const auto meshes = prepareMeshes(image, width, height);

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

    computeEngine->addTask({graph, formula, viewXRange, viewYRange, window->getWidth(), window->getHeight()});
}

std::vector<Mesh> Plot::prepareMeshes(const std::vector<int> &image, const int width, const int height)
{
    auto getGradent = [](const int value) -> float {
        return static_cast<float>(value > 0);
    };

    std::vector<float> data;

    std::ranges::transform(image, std::back_inserter(data), getGradent);

    const auto textureData = std::span<const float>{data};
    const auto textureResolution = staplegl::resolution{width, height};
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

void Plot::updateModelMat()
{
    const auto scale = glm::vec3{viewXRange.size() / plotXRange.size(), viewYRange.size() / plotYRange.size(), 1.0f};
    const auto scaleMat = glm::scale(glm::mat4(1.0), glm::vec3{1.0} / scale);
    const auto translate = glm::vec3{viewXRange.mid() - plotXRange.mid(), viewYRange.mid() - plotYRange.mid(), 0.0f};
    const auto translateMat = glm::translate(glm::mat4(1.0), -translate);

    const auto NDCToPlotMat = glm::scale(glm::mat4(1.0), glm::vec3{viewXRange.size() / 2.0, viewYRange.size() / 2.0, 1.0});
    const auto plotToViewMat = translateMat * scaleMat;
    const auto viewToNDCMat = glm::scale(glm::mat4(1.0), glm::vec3{2.0 / viewXRange.size(), 2.0 / viewYRange.size(), 1.0});
    model = viewToNDCMat * plotToViewMat * NDCToPlotMat;

    shader->bind();
    shader->upload_uniform_mat4f("transform", std::span<float, 16>{glm::value_ptr(model), 16});
}

void Plot::onCursorDrag(const double x, const double y)
{
    const auto windowWidth{window->getWidth()};
    const auto windowHeight{window->getHeight()};

    const auto deltaX{viewXRange.size() / windowWidth};
    const auto deltaY{viewYRange.size() / windowHeight};

    viewXRange = viewXRange + x * -deltaX;
    viewYRange = viewYRange + y * deltaY;

    plotRangeChangedCallback(viewXRange, viewYRange);

    updateModelMat();

    // TODO: make plotMesh a pointer and check if it is nullptr
    if (plotMesh.shader != nullptr)
    {
        plotMesh.shader->bind();
        plotMesh.shader->upload_uniform_mat4f("transform", std::span<float, 16>{glm::value_ptr(model), 16});
        plotCompleteCallback({plotMesh});
    }

    if (formula)
    {
        computeEngine->addTask({graph, formula, viewXRange, viewYRange, windowWidth, windowHeight});
    }
}

void Plot::onWindowSizeChanged(const int width, const int height)
{
    auto ratio = width / static_cast<double>(height);

    double xRangeSize = viewYRange.size() * ratio;

    double xRangeMid = (viewXRange.lower + viewXRange.upper) / 2.0;

    viewXRange = {xRangeMid - xRangeSize / 2.0, xRangeMid + xRangeSize / 2.0};

    plotRangeChangedCallback(viewXRange, viewYRange);

    updateModelMat();

    if (formula)
    {
        computeEngine->addTask({graph, formula, viewXRange, viewYRange, width, height});
    }
}

Interval<double> Plot::getXRanges() const
{
    return viewXRange;
}

Interval<double> Plot::getYRanges() const
{
    return viewYRange;
}

void Plot::onMouseScrolled(double offset)
{
    const auto windowWidth{window->getWidth()};
    const auto windowHeight{window->getHeight()};

    const auto xRangeMid = viewXRange.mid();
    const auto yRangeMid = viewYRange.mid();

    const auto xRangeSize = viewXRange.size() * (1.0 - offset * 0.1);
    const auto yRangeSize = viewYRange.size() * (1.0 - offset * 0.1);

    viewXRange = {xRangeMid - xRangeSize / 2.0, xRangeMid + xRangeSize / 2.0};
    viewYRange = {yRangeMid - yRangeSize / 2.0, yRangeMid + yRangeSize / 2.0};

    plotRangeChangedCallback(viewXRange, viewYRange);

    updateModelMat();

    if (formula)
    {
        computeEngine->addTask({graph, formula, viewXRange, viewYRange, windowWidth, windowHeight});
    }
}
