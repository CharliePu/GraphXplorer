//
// Created by charl on 6/11/2024.
//

#ifndef INPUTBOX_H
#define INPUTBOX_H
#include "UIComponent.h"

#include "../Render/Mesh.h"


class InputBox : public UIComponent
{
public:
    explicit InputBox(const std::shared_ptr<Window> &window);

    void prepareMesh();

    void setInputCompleteCallback(const std::function<void(std::string)> &callback);

    void setUpdateStateCallback(const std::function<void(const std::vector<Mesh> &)> &callback);

    void showInputBox();

    void hideInputBox();

    void updateTextDisplay();

    void onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods) override;

    void onTextEntered(unsigned int codepoint) override;

    int getDepth() const override;

    void onWindowSizeChanged(int width, int height) override;

private:
    bool isInputing;

    std::shared_ptr<Window> window;

    Mesh boxMesh;
    std::vector<Mesh> textMeshes;

    std::vector<Mesh> drawMeshes;

    std::string line;
    std::function<void(std::string)> inputCompleteCallback;
    std::function<void(const std::vector<Mesh> &)> updateStateCallback;
};


#endif //INPUTBOX_H
