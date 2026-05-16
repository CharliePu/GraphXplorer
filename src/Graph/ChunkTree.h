#ifndef CHUNKTREE_H
#define CHUNKTREE_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../Math/Interval.h"
#include "../Math/RasterizedPlot.h"
#include "../Graph/Graph.h"

struct ChunkKey
{
    int64_t x;
    int64_t y;
    int level;

    bool operator==(const ChunkKey &other) const
    {
        return x == other.x && y == other.y && level == other.level;
    }
};

struct ChunkKeyHash
{
    size_t operator()(const ChunkKey &key) const
    {
        return chunkKeyHash(key.x, key.y, key.level);
    }
};

struct TargetCell
{
    int64_t x;
    int64_t y;
    int level;
};

struct VisibleCover
{
    std::vector<ChunkKey> keys;
    std::vector<TargetCell> missingCells;
    size_t targetCellCount = 0;
    bool bounded = true;

    [[nodiscard]] bool complete() const
    {
        return bounded && missingCells.empty();
    }
};

class ChunkTree
{
public:
    struct Node
    {
        RasterChunk chunk;
        Node *parent = nullptr;
        std::array<Node *, 4> children = {nullptr, nullptr, nullptr, nullptr};
    };

    void clear();
    void insert(const RasterChunk &chunk);
    void remove(const ChunkKey &key);

    [[nodiscard]] Node *find(const ChunkKey &key) const;
    [[nodiscard]] const RasterChunk *findChunk(const ChunkKey &key) const;

    [[nodiscard]] const ChunkKey *findBestForCell(int64_t cellX, int64_t cellY, int targetLevel) const;
    [[nodiscard]] VisibleCover selectVisibleCover(
        const Interval &viewXRange, const Interval &viewYRange, int targetLevel) const;
    [[nodiscard]] std::vector<ChunkKey> selectVisible(
        const Interval &viewXRange, const Interval &viewYRange, int targetLevel) const;

    [[nodiscard]] size_t size() const { return nodes.size(); }

    [[nodiscard]] std::map<int, size_t> levelCounts() const;

    template<typename Fn>
    void forEach(const Fn &fn) const
    {
        for (const auto &[key, node] : nodes)
        {
            fn(key, node.chunk);
        }
    }

private:
    static int childIndex(int64_t parentX, int64_t parentY, int64_t childX, int64_t childY);
    static ChunkKey parentKey(const ChunkKey &key);
    static ChunkKey childKey(const ChunkKey &key, int index);

    void unlink(Node *node);
    void evictDescendants(Node *node);
    void validate(const char *context) const;

    std::unordered_map<ChunkKey, std::unique_ptr<Node>, ChunkKeyHash> nodes;
};

#endif // CHUNKTREE_H
