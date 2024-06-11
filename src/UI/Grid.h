//
// Created by charl on 6/5/2024.
//

#ifndef GRID_H
#define GRID_H

#include <staplegl/staplegl.hpp>

#include "UIComponent.h"

#include "../Math/Interval.h"
#include "../Render/Mesh.h"

class Grid: public UIComponent {
public:
    explicit Grid(const std::shared_ptr<Window> &window);

    void prepareMesh();

    void updatePosition(Interval<double> xInterval, Interval<double> yInterval);

    void setUpdatePositionCallback(const std::function<void(const std::vector<Mesh> &)> &callback);

    int getDepth() const override;

private:
    std::function<void(const std::vector<Mesh> &)> updatePositionCallback;

    std::shared_ptr<Window> window;

    Mesh mesh;
};



#endif //GRID_H
