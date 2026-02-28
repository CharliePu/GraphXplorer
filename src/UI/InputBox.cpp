//
// Created by charl on 6/11/2024.
//

#include "InputBox.h"

#include "../Core/Window.h"
#include "../Render/TextMeshesGenerator.h"
#include "staplegl/staplegl.hpp"

InputBox::InputBox(const std::shared_ptr<Window> &window): isInputing{false}, window{window}, isVisible{false}
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

    boxMesh = Mesh{
        std::make_shared<staplegl::shader_program>(std::move(shader)),
        vao,
        {},
        MeshPrimitive::Triangles,
        static_cast<int>(indices.size())
    };
}

void InputBox::setInputCompleteCallback(const std::function<void(std::string)> &callback)
{
    inputCompleteCallback = callback;
}

void InputBox::setUpdateStateCallback(const std::function<void(const std::vector<Mesh> &)> &callback)
{
    updateStateCallback = callback;
}

void InputBox::updateMeshes()
{
    drawMeshes.clear();
    drawMeshes.push_back(boxMesh);
    drawMeshes.insert(drawMeshes.end(), textMeshes.begin(), textMeshes.end());

    if (isVisible)
    {
        updateStateCallback(drawMeshes);
    }
    else
    {
        updateStateCallback({});
    }
}

void InputBox::showInputBox()
{
    isVisible = true;

    updateMeshes();
}

void InputBox::hideInputBox()
{
    isVisible = false;

    updateMeshes();
}

void InputBox::beginInput()
{
    if (isInputing)
    {
        return;
    }

    isInputing = true;
    showInputBox();
    updateTextDisplay();
}

void InputBox::updateTextDisplay()
{
    auto &textGenerator = TextMeshesGenerator::getInstance();

    textMeshes.clear();

    textMeshes = textGenerator.generateTextMesh(line, -0.95, -0.95, 0.003, TextAlign::LEFT,
                                                static_cast<double>(window->getWidth()) / window->getHeight());

    updateMeshes();
}

void InputBox::onKeyPressed(glfw::KeyCode key, int scancode, glfw::KeyState action, glfw::ModifierKeyBit mods)
{
    (void)scancode;
    (void)mods;

    if (!isInputing)
    {
        return;
    }

    if (action != glfw::KeyState::Press && action != glfw::KeyState::Repeat)
    {
        return;
    }

    if (key == glfw::KeyCode::Enter && action == glfw::KeyState::Press)
    {
        if (inputCompleteCallback)
        {
            inputCompleteCallback(line);
        }
        isInputing = false;
        hideInputBox();
    }
    else if (key == glfw::KeyCode::Backspace)
    {
        if (!line.empty())
        {
            line.pop_back();
            updateTextDisplay();
        }
    }
    else if (key == glfw::KeyCode::Escape)
    {
        isInputing = false;
        hideInputBox();
    }
}

void InputBox::onTextEntered(unsigned int codepoint)
{
    if (!isInputing)
    {
        return;
    }

    if (isAllowedInputCharacter(codepoint))
    {
        line.push_back(static_cast<char>(codepoint));
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

bool InputBox::isCapturingInput() const
{
    return isInputing;
}

bool InputBox::isAllowedInputCharacter(unsigned int codepoint)
{
    if (codepoint < 0x20 || codepoint == 0x7F)
    {
        return false;
    }

    return codepoint <= 0x7E;
}
