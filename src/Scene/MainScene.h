//
// Created by charl on 6/3/2024.
//

#ifndef MAINSCENE_H
#define MAINSCENE_H
#include "Scene.h"


class ComputeEngine;
class Renderer;
class ConsoleInput;
class Plot;

class MainScene: public Scene {
public:
    MainScene(const std::shared_ptr<ComputeEngine>& engine, const std::shared_ptr<Renderer>& renderer, const std::shared_ptr<Window> &window);

    void onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods) override;

    void onCursorDrag(double x, double y) override;

    void onWindowSizeChanged(int width, int height) override;

private:

    std::shared_ptr<Plot> plot;
    std::shared_ptr<ConsoleInput> cmd;
};



#endif //MAINSCENE_H
