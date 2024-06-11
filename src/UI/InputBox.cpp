//
// Created by charl on 6/11/2024.
//

#include "InputBox.h"

#include <unordered_set>

#include "../Core/Window.h"
#include "../Render/TextMeshesGenerator.h"
#include "staplegl/staplegl.hpp"

InputBox::InputBox(const std::shared_ptr<Window> &window): isInputing{false}, window{window}
{
    prepareMesh();
}

void InputBox::prepareMesh()
{
    auto vao = std::make_shared<staplegl::vertex_array>();

    std::array vertices = {
        1.0f, -0.8f, 0.0f, // top right
        1.0f, -1.0f, 0.0f, // bottom right
        -1.0f, -1.0f, 0.0f, // bottom left
        -1.0f, -0.8f, 0.0f // top left
    };

    std::array indices = {
        0u, 1u, 3u, // first Triangle
        1u, 2u, 3u // second Triangle
    };

    staplegl::vertex_buffer_layout layout{{staplegl::shader_data_type::u_type::vec3, "aPos"}};
    staplegl::vertex_buffer vbo{vertices, staplegl::driver_draw_hint::STATIC_DRAW};
    vbo.set_layout(layout);

    staplegl::index_buffer ebo{indices};

    vao->add_vertex_buffer(std::move(vbo));
    vao->set_index_buffer(std::move(ebo));

    staplegl::shader_program shader{
        "input_box_shader",
        {
            std::pair{staplegl::shader_type::vertex, "./shader/inputBox.vert"},
            std::pair{staplegl::shader_type::fragment, "./shader/inputBox.frag"}
        }
    };

    boxMesh = Mesh{std::make_shared<staplegl::shader_program>(std::move(shader)), vao, {}};
}

void InputBox::setInputCompleteCallback(const std::function<void(std::string)> &callback)
{
    inputCompleteCallback = callback;
}

void InputBox::setUpdateStateCallback(const std::function<void(const std::vector<Mesh> &)> &callback)
{
    updateStateCallback = callback;
}

void InputBox::showInputBox()
{
    std::vector<Mesh> meshes{boxMesh};
    meshes.insert(meshes.end(), textMeshes.begin(), textMeshes.end());

    updateStateCallback(meshes);
}

void InputBox::hideInputBox()
{
    updateStateCallback({});
}

void InputBox::updateTextDisplay()
{
    auto &textGenerator = TextMeshesGenerator::getInstance();

    textMeshes.clear();

    textMeshes = textGenerator.generateTextMesh(line, -0.95, -0.95, 0.003, TextAlign::LEFT,
                                                static_cast<double>(window->getWidth()) / window->getHeight());

    std::vector<Mesh> meshes{boxMesh};
    meshes.insert(meshes.end(), textMeshes.begin(), textMeshes.end());

    updateStateCallback(meshes);
}

void InputBox::onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods)
{
    if (!isInputing)
    {
        if (key == glfw::KeyCode::I && action == glfw::KeyState::Press)
        {
            isInputing = true;
            showInputBox();
        }
    }
    else
    {
        if (key == glfw::KeyCode::I && action == glfw::KeyState::Press)
        {
            isInputing = false;
            hideInputBox();
        }
        else if (key == glfw::KeyCode::Enter && action == glfw::KeyState::Press)
        {
            inputCompleteCallback(line);
            isInputing = false;
            hideInputBox();
        }
        else if (key == glfw::KeyCode::Backspace && action == glfw::KeyState::Press)
        {
            if (!line.empty())
            {
                line.pop_back();
                updateTextDisplay();
            }
        }
        else
        if (key == glfw::KeyCode::Enter && action == glfw::KeyState::Press)
        {
            inputCompleteCallback(line);
            isInputing = false;
            hideInputBox();
        }
        else if (key == glfw::KeyCode::Backspace && action == glfw::KeyState::Press)
        {
            if (!line.empty())
            {
                line.pop_back();
                updateTextDisplay();
            }
        }
    }
}

void InputBox::onTextEntered(unsigned int codepoint)
{
    if (isInputing)
    {
        if (codepoint >= glfw::KeyCode::Zero && codepoint <= glfw::KeyCode::Nine)
        {
            line.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint == 0x3C || codepoint == 0x3E || codepoint == 0x3D) // <. >, =
        {
            line.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint == 0x2B || codepoint == 0x2D || codepoint == 0x2A || codepoint == 0x2F) // +, -, *, /
        {
            line.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint == 0x28 || codepoint == 0x29) // (, )
        {
            line.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint == 0x5E) // ^
        {
            line.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint == 0x2E) // .
        {
            line.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint == 0x58 || codepoint == 0x59 || codepoint == 0x78 || codepoint == 0x79) // x, y, X, Y
        {
            line.push_back(static_cast<char>(codepoint));
        }

        updateTextDisplay();
    }
}

int InputBox::getDepth() const
{
    return 5;
}

void InputBox::onWindowSizeChanged(int width, int height)
{
    updateTextDisplay();
}
