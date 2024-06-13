//
// Created by charl on 6/3/2024.
//

#ifndef MAINSCENE_H
#define MAINSCENE_H
#include "Scene.h"


class GraphRasterizer;
class InputBox;
class AxisLabels;
class Grid;
class GraphProcessor;
class Renderer;
class ConsoleInput;
class Plot;

class MainScene: public Scene {
public:
    MainScene(const std::shared_ptr<GraphProcessor>& processor, const std::shared_ptr<GraphRasterizer> &rasterizer, const std::shared_ptr<Renderer>& renderer, const std::shared_ptr<Window> &window);

    void onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods) override;

    void onCursorDrag(double x, double y) override;

    void onWindowSizeChanged(int width, int height) override;

    void onTextEntered(unsigned int codepoint) override;

private:

    std::shared_ptr<Plot> plot;
    std::shared_ptr<Grid> grid;
    std::shared_ptr<AxisLabels> axisLabels;
    std::shared_ptr<ConsoleInput> cmd;
    std::shared_ptr<InputBox> inputBox;
};



#endif //MAINSCENE_H
