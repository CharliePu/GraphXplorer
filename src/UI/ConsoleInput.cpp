//
// Created by charl on 6/3/2024.
//

#include "ConsoleInput.h"

#include <future>
#include <iostream>

void ConsoleInput::inputFromConsole()
{
    // std::cout<<"Enter formula: ";
    // std::getline(std::cin, line);
    // inputCompleteCallback(line);

    inputCompleteCallback("x+yy<25");
}

void ConsoleInput::setInputCompleteCallback(const std::function<void(std::string)> &callback)
{
    inputCompleteCallback = callback;
}

void ConsoleInput::onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods)
{
    if (key == glfw::KeyCode::I && action == glfw::KeyState::Press)
    {
        inputFromConsole();
    }
}
