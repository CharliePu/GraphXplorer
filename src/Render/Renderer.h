//
// Created by charl on 6/2/2024.
//

#ifndef RENDERER_H
#define RENDERER_H
#include <map>
#include <span>
#include <glad/glad.h>

#include "Mesh.h"
#include "FrameCommandBuffer.h"

namespace gx
{
class RenderResourceManager;
}

class Renderer {
public:
    explicit Renderer(const GLADloadproc &gladLoader);

    void clear();
    void draw();
    void draw(const gx::FrameCommandBuffer &commands);
    void draw(std::span<const gx::DrawCommand> commands);
    void setResourceManager(gx::RenderResourceManager *resources);

    void updateMeshes(int layer, const std::vector<Mesh>& meshes);

    void onWindowSizeChanged(int width, int height);
private:
    void draw(const std::vector<Mesh> &meshes);

    std::map<int, std::vector<Mesh>> layerMeshes;
    gx::RenderResourceManager *resources{nullptr};
};



#endif //RENDERER_H
