//
// Created by charl on 6/2/2024.
//

#ifndef PLOT_H
#define PLOT_H

#include "UIComponent.h"
#include "../Core/Input.h"
#include "../Math/Interval.h"

namespace staplegl
{
    class shader_program;
    class vertex_array;
}

class GraphRasterizer;
class Formula;
class Graph;
class ComputeEngine;

class Plot: public UIComponent {

public:
    void prepareVertices() const;

    Plot(const std::shared_ptr<ComputeEngine> &engine, const std::shared_ptr<Window> &window);

    void setPlotCompleteCallback(const std::function<void(const std::vector<Mesh> &)> &callback);

    int getDepth() const override;

    void requestNewPlot(const std::string & input);

    std::vector<Mesh> prepareMeshes(const std::vector<Interval<bool>> & image) const;

    void onCursorDrag(double x, double y) override;

    void onWindowSizeChanged(int width, int height) override;

    Interval<double> getXRanges() const;

    Interval<double> getYRanges() const;

private:
    std::shared_ptr<Graph> graph;
    std::shared_ptr<Formula> formula;
    std::shared_ptr<ComputeEngine> computeEngine;
    std::shared_ptr<GraphRasterizer> graphRasterizer;
    std::shared_ptr<Window> window;

    std::function<void(const std::vector<Mesh> &)> plotCompleteCallback;

    Interval<double> xRange, yRange;

    std::shared_ptr<staplegl::vertex_array> vao;
    std::shared_ptr<staplegl::shader_program> shader;
};

#endif //PLOT_H
