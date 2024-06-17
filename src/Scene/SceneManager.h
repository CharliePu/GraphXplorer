//
// Created by charl on 6/3/2024.
//

#ifndef SCENEMANAGER_H
#define SCENEMANAGER_H
#include "../Core/Input.h"


class ComputeEngine;
class MainScene;
class Renderer;
class Scene;

class SceneManager : public UserInputHandler
{
public:
    SceneManager(
        const std::shared_ptr<ComputeEngine> &engine,
        const std::shared_ptr<Renderer> &renderer,
        const std::shared_ptr<Window> &window);

    void onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods) override;

    void onCursorDrag(double x, double y) override;

    void onWindowSizeChanged(int width, int height) override;

    void onTextEntered(unsigned int codepoint) override;

private:
    std::shared_ptr<MainScene> mainScene;
    std::shared_ptr<Scene> currentScene;
};


#endif //SCENEMANAGER_H
