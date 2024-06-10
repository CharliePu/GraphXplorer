//
// Created by charl on 6/8/2024.
//

#ifndef AXISLABELS_H
#define AXISLABELS_H
#include "Plot.h"
#include "UIComponent.h"


class AxisLabels: public UIComponent {
public:
    AxisLabels(const std::shared_ptr<Window> &window);

    int getDepth() const override;

    void updateLabels(Interval<double> xRange, Interval<double> yRange);

    void setUpdateLabelsCallback(const std::function<void(const std::vector<Mesh> &)>& callback);

private:
    std::function<void(const std::vector<Mesh> &)> updateLabelsCallback;

    std::shared_ptr<Window> window;
};



#endif //AXISLABELS_H
