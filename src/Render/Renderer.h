//
// Created by charl on 6/2/2024.
//

#ifndef RENDERER_H
#define RENDERER_H
#include <filesystem>
#include <span>
#include <glad/glad.h>

#include "FrameCommandBuffer.h"

namespace gx
{
class RenderResourceManager;
}

class Renderer {
public:
    explicit Renderer(const GLADloadproc &gladLoader);

    void clear();
    void draw(const gx::FrameCommandBuffer &commands, const gx::UploadBudget &uploadBudget = gx::UploadBudget{});
    void draw(std::span<const gx::DrawCommand> commands, const gx::UploadBudget &uploadBudget = gx::UploadBudget{});
    void setResourceManager(gx::RenderResourceManager *resources);

    void onWindowSizeChanged(int width, int height);
    [[nodiscard]] bool saveBackbufferPng(const std::filesystem::path &path) const;

private:
    gx::RenderResourceManager *resources{nullptr};
    int viewportWidth{0};
    int viewportHeight{0};
};



#endif //RENDERER_H
