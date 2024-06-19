//
// Created by charl on 6/2/2024.
//

#ifndef PLOT_H
#define PLOT_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "UIComponent.h"
#include "../Core/Input.h"
#include "../Math/Interval.h"
#include "../Render/Mesh.h"

class ComputeEngine;

namespace staplegl
{
    class shader_program;
    class vertex_array;
}

class Formula;
struct Graph;

class Plot: public UIComponent {

public:
    void prepareVertices() const;

    Plot(const std::shared_ptr<ComputeEngine> &engine, const std::shared_ptr<Window> &window);

    void setPlotCompleteCallback(const std::function<void(const std::vector<Mesh> &)> &callback);

    void setPlotRangeChangedCallback(const std::function<void(const Interval<double> &, const Interval<double> &)> &callback);

    int getDepth() const override;

    void requestNewPlot(const std::string & input);

    std::vector<Mesh> prepareMeshes(const std::vector<int> &image, const int width, const int height);

    void updateModelMat();

    void onCursorDrag(double x, double y) override;

    void onWindowSizeChanged(int width, int height) override;

    Interval<double> getXRanges() const;

    Interval<double> getYRanges() const;

    void onMouseScrolled(double offset) override;

private:
    std::shared_ptr<Graph> graph;
    std::shared_ptr<Formula> formula;
    std::shared_ptr<ComputeEngine> computeEngine;
    std::shared_ptr<Window> window;

    std::function<void(const std::vector<Mesh> &)> plotCompleteCallback;
    std::function<void(const Interval<double> &, const Interval<double> &)> plotRangeChangedCallback;

    Interval<double> viewXRange, viewYRange, plotXRange, plotYRange;

    std::shared_ptr<staplegl::vertex_array> vao;
    std::shared_ptr<staplegl::shader_program> shader;

    Mesh plotMesh;

    glm::mat4 model;
};

#endif //PLOT_H
