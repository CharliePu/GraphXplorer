//
// Created by charl on 6/2/2024.
//

#include "Renderer.h"

#include <iostream>
#include <span>
#include <glad/glad.h>
#include <staplegl/staplegl.hpp>

#include "RenderResourceManager.h"

Renderer::Renderer(const GLADloadproc &gladLoader)
{
    // Load OpenGL function pointers
    if (!gladLoadGLLoader(gladLoader))
    {
        throw std::runtime_error("Failed to initialize GLAD");
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glClearColor(0.12, 0.12, 0.14, 1.0);
}

void Renderer::clear()
{
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::draw()
{
    for (const auto &[layer, meshes]: layerMeshes)
    {
        (void)layer;
        draw(meshes);
    }
}

void Renderer::draw(const gx::FrameCommandBuffer &commands)
{
    draw(commands.commands());
}

void Renderer::draw(std::span<const gx::DrawCommand> commands)
{
    if (!resources)
    {
        return;
    }

    for (const auto &command : commands)
    {
        resources->draw(command);
    }
}

void Renderer::setResourceManager(gx::RenderResourceManager *nextResources)
{
    resources = nextResources;
}

void Renderer::updateMeshes(const int layer, const std::vector<Mesh> &meshes)
{
    layerMeshes[layer] = meshes;
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
        if (mesh.hasMeshTransform)
        {
            auto transformData = mesh.meshTransform;
            mesh.shader->upload_uniform_mat4f(
                "meshTransform", std::span<float, 16>{transformData.data(), 16});
        }
        mesh.vao->bind();
        for (int i = 0; i < mesh.textures.size(); i++)
        {
            glActiveTexture(GL_TEXTURE0 + i);
            mesh.textures[i]->bind();
        }

        const auto primitive = mesh.primitive == MeshPrimitive::Lines ? GL_LINES : GL_TRIANGLES;
        if (mesh.primitive == MeshPrimitive::Lines)
        {
            glLineWidth(mesh.lineWidth);
        }
        if (mesh.indexed)
        {
            if (mesh.elementCount >= 0)
            {
                glDrawElements(primitive, mesh.elementCount, GL_UNSIGNED_INT, nullptr);
            }
        }
        else
        {
            if (mesh.vertexCount >= 0)
            {
                glDrawArrays(primitive, 0, mesh.vertexCount);
            }
        }
        if (mesh.primitive == MeshPrimitive::Lines)
        {
            glLineWidth(1.0f);
        }
    }
}
