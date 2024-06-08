//
// Created by charl on 6/2/2024.
//

#include "Renderer.h"

#include <iostream>
#include <glad/glad.h>
#include <staplegl/staplegl.hpp>

Renderer::Renderer(const GLADloadproc &gladLoader)
{
    // Load OpenGL function pointers
    if (!gladLoadGLLoader(gladLoader))
    {
        throw std::runtime_error("Failed to initialize GLAD");
    }

    glClearColor(0,0,0,1);
}

void Renderer::clear()
{
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::draw()
{
    for (const auto &[component, meshes]: components)
    {
        draw(meshes);
    }
}

void Renderer::updateMeshes(const std::shared_ptr<UIComponent> &component, const std::vector<Mesh> &meshes)
{
    components[component] = meshes;
}

void Renderer::onWindowSizeChanged(int width, int height)
{
    glViewport(0, 0, width, height);
}

void Renderer::draw(const std::vector<Mesh> &meshes)
{
    for (const auto &mesh: meshes)
    {
        mesh.shader->bind();
        mesh.vao->bind();
        for (int i = 0; i < mesh.textures.size(); i++)
        {
            glActiveTexture(GL_TEXTURE0 + i);
            mesh.textures[i]->bind();
        }
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    }
}
