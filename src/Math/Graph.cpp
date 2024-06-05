#include "Graph.h"

#include <algorithm>
#include <queue>

GraphNode::GraphNode(GraphNode *parent, const Interval<double> &xRange,
                     const Interval<double> &yRange) : parent{parent}, children{}, solution{IntervalValues::Unknown},
                                                       xRange{xRange}, yRange{yRange}
{
}

bool GraphNode::isLeaf() const
{
    return std::ranges::all_of(children, [](const auto &child) { return child == nullptr; });
}

void GraphNode::subdivide()
{
    if (!isLeaf())
        return;

    auto midX = (xRange.lower + xRange.upper) / 2.0;
    auto midY = (yRange.lower + yRange.upper) / 2.0;

    children[0] = std::make_unique<GraphNode>(this, Interval<double>{xRange.lower, midX},
                                              Interval<double>{yRange.lower, midY});
    children[1] = std::make_unique<GraphNode>(this, Interval<double>{midX, xRange.upper},
                                              Interval<double>{yRange.lower, midY});
    children[2] = std::make_unique<GraphNode>(this, Interval<double>{xRange.lower, midX},
                                              Interval<double>{midY, yRange.upper});
    children[3] = std::make_unique<GraphNode>(this, Interval<double>{midX, xRange.upper},
                                              Interval<double>{midY, yRange.upper});
}

GraphNode *GraphNode::findChild(const Interval<double> &xRange, const Interval<double> &yRange) const
{
    for (const auto &child: children)
    {
        if (child && child->xRange.contains(xRange) && child->yRange.contains(yRange))
        {
            return child.get();
        }
    }
    throw std::invalid_argument("Child not found");
}

int GraphNode::findChildIndex(const Interval<double> &xRange, const Interval<double> &yRange) const
{
    for (int i = 0; i < children.size(); ++i)
    {
        if (children[i] && children[i]->xRange.contains(xRange) && children[i]->yRange.contains(yRange))
        {
            return i;
        }
    }
    throw std::invalid_argument("Child not found");
}

bool GraphNode::matches(const Interval<double> &otherXRange, const Interval<double> &otherYRange) const
{
    return (xRange == otherXRange) && (yRange == otherYRange);
}

Interval<double> Graph::getXRange() const
{
    return root->xRange;
}

Interval<double> Graph::getYRange() const
{
    return root->yRange;
}

bool Graph::empty() const
{
    return !root;
}

Interval<bool> Graph::evaluate(const Interval<double> &x, const Interval<double> &y) const
{
    std::queue<GraphNode *> nodeQueue;
    nodeQueue.push(root.get());

    while (!nodeQueue.empty())
    {
        auto curr = nodeQueue.front();
        nodeQueue.pop();

        if (curr->isLeaf())
        {
            return curr->solution;
        }

        for (const auto &child: curr->children)
        {
            if (child)
            {
                if (child->xRange.contains(x) && child->yRange.contains(y))
                {
                    nodeQueue.push(child.get());
                }
            }
        }
    }

    throw std::invalid_argument("Failed to evaluate graph");
}
