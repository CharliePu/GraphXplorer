#ifndef TEXTATLAS_H
#define TEXTATLAS_H

#include <string>
#include <utility>
#include <vector>

#include "../Util/Contracts.h"

namespace gx
{
struct TextRun
{
    std::string text{};
    double x{0.0};
    double y{0.0};
    double scale{1.0};
};

class TextAtlas
{
public:
    void clearRuns()
    {
        runs.clear();
    }

    void submit(TextRun run)
    {
        runs.push_back(std::move(run));
    }

    [[nodiscard]] size_t runCount() const
    {
        return runs.size();
    }

private:
    std::vector<TextRun> runs;
};
}

#endif // TEXTATLAS_H
