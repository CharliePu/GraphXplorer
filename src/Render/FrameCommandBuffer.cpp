#include "FrameCommandBuffer.h"

#include <algorithm>

namespace gx
{
void FrameCommandBuffer::clear()
{
    drawCommands.clear();
    isFrozen = false;
}

bool FrameCommandBuffer::add(DrawCommand command)
{
    if (isFrozen)
    {
        return false;
    }

    drawCommands.push_back(command);
    return true;
}

void FrameCommandBuffer::freezeAndSort()
{
    std::ranges::sort(drawCommands, [](const DrawCommand &lhs, const DrawCommand &rhs)
    {
        if (lhs.layer != rhs.layer)
        {
            return static_cast<int>(lhs.layer) < static_cast<int>(rhs.layer);
        }
        if (lhs.sortKey != rhs.sortKey)
        {
            return lhs.sortKey < rhs.sortKey;
        }
        if (lhs.pipeline.id != rhs.pipeline.id)
        {
            return lhs.pipeline.id < rhs.pipeline.id;
        }
        if (lhs.material.id != rhs.material.id)
        {
            return lhs.material.id < rhs.material.id;
        }
        if (lhs.textures.id != rhs.textures.id)
        {
            return lhs.textures.id < rhs.textures.id;
        }
        return lhs.geometry.id < rhs.geometry.id;
    });
    isFrozen = true;
}

bool FrameCommandBuffer::frozen() const
{
    return isFrozen;
}

std::span<const DrawCommand> FrameCommandBuffer::commands() const
{
    return drawCommands;
}
}
