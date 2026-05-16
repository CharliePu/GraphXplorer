#ifndef FRAMECOMMANDBUFFER_H
#define FRAMECOMMANDBUFFER_H

#include <span>
#include <vector>

#include "../Util/Contracts.h"

namespace gx
{
class FrameCommandBuffer
{
public:
    void clear();
    bool add(DrawCommand command);
    void freezeAndSort();

    [[nodiscard]] bool frozen() const;
    [[nodiscard]] std::span<const DrawCommand> commands() const;

private:
    std::vector<DrawCommand> drawCommands;
    bool isFrozen{false};
};
}

#endif // FRAMECOMMANDBUFFER_H
