//
// Created by charl on 6/5/2024.
//

#ifndef GRID_H
#define GRID_H

#include <staplegl/staplegl.hpp>

#include "UIComponent.h"

#include "../Math/Interval.h"

class Grid: public UIComponent {
public:
    explicit Grid(const std::shared_ptr<Window> &window);

    void prepareVertices() const;

    void updatePosition(Interval<double> xInterval, Interval<double> yInterval);

    std::vector<Mesh> prepareMeshes(const std::vector<float> &data);

    void setUpdatePositionCallback(const std::function<void(const std::vector<Mesh> &)> &callback);

    int getDepth() const override;

private:
    std::function<void(const std::vector<Mesh> &)> updatePositionCallback;

    std::shared_ptr<Window> window;

    std::shared_ptr<staplegl::vertex_array> vao;
    std::shared_ptr<staplegl::shader_program> shader;
};



#endif //GRID_H
