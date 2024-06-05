//
// Created by charl on 6/3/2024.
//

#ifndef CMD_H
#define CMD_H
#include "UIComponent.h"


class ConsoleInput: public UIComponent {
public:
    void inputFromConsole();
    void setInputCompleteCallback(const std::function<void(std::string)> &callback);

    void onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods) override;

private:
    std::string line;
    std::function<void(std::string)> inputCompleteCallback;
};



#endif //CMD_H
