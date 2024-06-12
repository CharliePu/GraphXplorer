//
// Created by charl on 6/3/2024.
//

#include "ConsoleInput.h"

#include <future>
#include <iostream>
#include <string>

void ConsoleInput::inputFromConsole()
{
    std::cout<<"Enter formula: ";
    std::getline(std::cin, line);
    inputCompleteCallback(line);
}

void ConsoleInput::setInputCompleteCallback(const std::function<void(std::string)> &callback)
{
    inputCompleteCallback = callback;
}

void ConsoleInput::onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods)
{
    if (key == glfw::KeyCode::C && action == glfw::KeyState::Press)
    {
        inputFromConsole();
    }
}
