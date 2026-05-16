#include "ChunkTree.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <tuple>

#include "../Util/PipelineLog.h"

namespace
{
constexpr size_t kMaxVisibleCells = 8192;
constexpr size_t kMaxVisibleCellsPerAxis = 2048;

bool hasFiniteRange(const Interval &range)
{
    return std::isfinite(range.lower)
        && std::isfinite(range.upper)
        && range.upper > range.lower;
}

bool checkedVisibleGridSize(const int64_t minCX, const int64_t maxCX,
                            const int64_t minCY, const int64_t maxCY,
                            size_t &cols, size_t &rows)
{
    if (maxCX < minCX || maxCY < minCY)
    {
        return false;
    }

    const auto colSpan = static_cast<long double>(maxCX) - static_cast<long double>(minCX) + 1.0L;
    const auto rowSpan = static_cast<long double>(maxCY) - static_cast<long double>(minCY) + 1.0L;
    if (colSpan <= 0.0L || rowSpan <= 0.0L
        || colSpan > static_cast<long double>(kMaxVisibleCellsPerAxis)
        || rowSpan > static_cast<long double>(kMaxVisibleCellsPerAxis))
    {
        return false;
    }

    cols = static_cast<size_t>(colSpan);
    rows = static_cast<size_t>(rowSpan);
    return rows == 0 || cols <= kMaxVisibleCells / rows;
}
}

void ChunkTree::clear()
{
    nodes.clear();
}

void ChunkTree::unlink(Node *node)
{
    if (!node) return;

    if (node->parent)
    {
        for (auto &child : node->parent->children)
        {
            if (child == node)
            {
                child = nullptr;
                break;
            }
        }
        node->parent = nullptr;
    }

    for (auto &child : node->children)
    {
        if (child)
        {
            child->parent = nullptr;
            child = nullptr;
        }
    }
}

void ChunkTree::evictDescendants(Node *node)
{
    if (!node) return;

    for (auto &child : node->children)
    {
        if (!child) continue;

        auto *childNode = child;
        const ChunkKey childNodeKey{
            childNode->chunk.chunkX,
            childNode->chunk.chunkY,
            childNode->chunk.level
        };

        evictDescendants(childNode);
        unlink(childNode);
        nodes.erase(childNodeKey);
        child = nullptr;
    }
}

int ChunkTree::childIndex(const int64_t parentX, const int64_t parentY,
                          const int64_t childX, const int64_t childY)
{
    return static_cast<int>((childX & 1) + 2 * (childY & 1));
}

ChunkKey ChunkTree::parentKey(const ChunkKey &key)
{
    return {floorDivByPow2(key.x, 1), floorDivByPow2(key.y, 1), key.level + 1};
}

ChunkKey ChunkTree::childKey(const ChunkKey &key, const int index)
{
    return {key.x * 2 + (index & 1), key.y * 2 + ((index >> 1) & 1), key.level - 1};
}

void ChunkTree::validate(const char *context) const
{
    for (const auto &[key, node] : nodes)
    {
        if (!node)
        {
            PipelineLog::log("TREE CORRUPTION: null node at (%lld,%lld,%d) ctx=%s",
                key.x, key.y, key.level, context);
            assert(false && "null node in tree");
        }

        if (node->parent)
        {
            bool foundInParent = false;
            for (const auto &c : node->parent->children)
            {
                if (c == node.get()) { foundInParent = true; break; }
            }
            if (!foundInParent)
            {
                PipelineLog::log("TREE CORRUPTION: node (%lld,%lld,%d) parent doesn't reference it ctx=%s",
                    key.x, key.y, key.level, context);
                assert(false && "parent doesn't reference child");
            }
        }

        for (int i = 0; i < 4; ++i)
        {
            if (!node->children[i]) continue;

            const auto &child = node->children[i];
            ChunkKey cKey{child->chunk.chunkX, child->chunk.chunkY, child->chunk.level};

            if (!nodes.contains(cKey))
            {
                PipelineLog::log("TREE CORRUPTION: node (%lld,%lld,%d) child[%d]=(%lld,%lld,%d) NOT in map ctx=%s",
                    key.x, key.y, key.level, i, cKey.x, cKey.y, cKey.level, context);
                assert(false && "child pointer not in map");
            }

            if (child->parent != node.get())
            {
                PipelineLog::log("TREE CORRUPTION: node (%lld,%lld,%d) child[%d] parent mismatch ctx=%s",
                    key.x, key.y, key.level, i, context);
                assert(false && "child parent mismatch");
            }
        }
    }
}

void ChunkTree::insert(const RasterChunk &chunk)
{
    const ChunkKey key{chunk.chunkX, chunk.chunkY, chunk.level};

    auto [it, inserted] = nodes.try_emplace(key, nullptr);
    if (inserted)
    {
        it->second = std::make_unique<Node>();
    }

    auto *node = it->second.get();

    const ChunkKey pKey = parentKey(key);
    Node *parentNode = nullptr;
    int parentIdx = -1;
    if (auto pParent = nodes.find(pKey); pParent != nodes.end())
    {
        parentNode = pParent->second.get();
        parentIdx = childIndex(pKey.x, pKey.y, key.x, key.y);
    }

    if (node->parent)
    {
        unlink(node);
    }

    node->chunk = chunk;

    if (parentNode && nodes.contains(pKey))
    {
        parentNode->children[parentIdx] = node;
        node->parent = parentNode;
    }

    for (int i = 0; i < 4; ++i)
    {
        const ChunkKey cKey = childKey(key, i);
        if (auto cIt = nodes.find(cKey); cIt != nodes.end())
        {
            auto *childNode = cIt->second.get();
            node->children[i] = childNode;
            childNode->parent = node;
        }
    }

    validate("after insert");
}

void ChunkTree::remove(const ChunkKey &key)
{
    auto it = nodes.find(key);
    if (it == nodes.end()) return;

    auto *node = it->second.get();
    evictDescendants(node);
    unlink(node);
    nodes.erase(it);
}

ChunkTree::Node *ChunkTree::find(const ChunkKey &key) const
{
    auto it = nodes.find(key);
    return it != nodes.end() ? it->second.get() : nullptr;
}

const RasterChunk *ChunkTree::findChunk(const ChunkKey &key) const
{
    auto *node = find(key);
    return node ? &node->chunk : nullptr;
}

const ChunkKey *ChunkTree::findBestForCell(const int64_t cellX, const int64_t cellY,
                                           const int targetLevel) const
{
    const ChunkKey exactKey{cellX, cellY, targetLevel};
    if (nodes.contains(exactKey))
    {
        return &nodes.find(exactKey)->first;
    }

    ChunkKey current = exactKey;
    while (current.level < MAX_CHUNK_LEVEL)
    {
        current = parentKey(current);
        auto it = nodes.find(current);
        if (it != nodes.end())
        {
            return &it->first;
        }
    }

    return nullptr;
}

VisibleCover ChunkTree::selectVisibleCover(
    const Interval &viewXRange, const Interval &viewYRange, const int targetLevel) const
{
    VisibleCover cover;
    const auto level = clampChunkLevel(targetLevel);

    if (!hasFiniteRange(viewXRange) || !hasFiniteRange(viewYRange))
    {
        cover.bounded = false;
        return cover;
    }

    validate("before selectVisibleCover");

    const auto [minCX, maxCX] = chunkIndexBounds(viewXRange, level);
    const auto [minCY, maxCY] = chunkIndexBounds(viewYRange, level);

    size_t cols = 0;
    size_t rows = 0;
    if (!checkedVisibleGridSize(minCX, maxCX, minCY, maxCY, cols, rows))
    {
        PipelineLog::log("selectVisibleCover: reject level=%d bounds=(%lld,%lld)x(%lld,%lld)",
            level, minCX, maxCX, minCY, maxCY);
        cover.bounded = false;
        return cover;
    }

    cover.targetCellCount = cols * rows;

    std::unordered_set<ChunkKey, ChunkKeyHash> chosenSet;
    chosenSet.reserve(cols * rows);
    cover.missingCells.reserve(cols * rows);

    std::vector<ChunkKey> nodeKeys;
    nodeKeys.reserve(nodes.size());
    for (const auto &[key, _] : nodes)
    {
        nodeKeys.push_back(key);
    }

    const auto hasAnyDescendant = [&nodeKeys](const ChunkKey &ancestor)
    {
        return std::ranges::any_of(nodeKeys, [&ancestor](const ChunkKey &candidate)
        {
            return candidate.level < ancestor.level
                && parentCoversChild(ancestor.x, ancestor.y, ancestor.level,
                                     candidate.x, candidate.y, candidate.level);
        });
    };

    const auto nearestAncestor = [this](const ChunkKey &cell) -> const ChunkKey *
    {
        auto current = cell;
        while (current.level < MAX_CHUNK_LEVEL)
        {
            current = parentKey(current);
            const auto it = nodes.find(current);
            if (it != nodes.end())
            {
                return &it->first;
            }
        }

        return nullptr;
    };

    const auto collectDescendantCover =
        [this, &hasAnyDescendant](const ChunkKey &cell, std::vector<ChunkKey> &keys)
    {
        const auto collectImpl =
            [this, &hasAnyDescendant](const ChunkKey &current,
                                      std::vector<ChunkKey> &collected,
                                      const auto &self) -> bool
        {
            const auto exactIt = nodes.find(current);
            if (exactIt != nodes.end())
            {
                collected.push_back(exactIt->first);
                return true;
            }

            if (current.level <= MIN_CHUNK_LEVEL || !hasAnyDescendant(current))
            {
                return false;
            }

            const auto originalSize = collected.size();
            for (int i = 0; i < 4; ++i)
            {
                const auto child = childKey(current, i);
                if (!self(child, collected, self))
                {
                    collected.resize(originalSize);
                    return false;
                }
            }

            return true;
        };

        return collectImpl(cell, keys, collectImpl);
    };

    for (auto cy = minCY; cy <= maxCY; ++cy)
    {
        for (auto cx = minCX; cx <= maxCX; ++cx)
        {
            const ChunkKey targetCell{cx, cy, level};
            if (const auto exactIt = nodes.find(targetCell); exactIt != nodes.end())
            {
                chosenSet.insert(exactIt->first);
                continue;
            }

            if (const auto *ancestor = nearestAncestor(targetCell))
            {
                chosenSet.insert(*ancestor);
                continue;
            }

            std::vector<ChunkKey> descendantCover;
            if (collectDescendantCover(targetCell, descendantCover))
            {
                chosenSet.insert(descendantCover.begin(), descendantCover.end());
            }
            else
            {
                cover.missingCells.push_back({cx, cy, level});
            }
        }
    }

    std::vector<ChunkKey> keys;
    keys.reserve(chosenSet.size());
    for (const auto &key : chosenSet)
    {
        keys.push_back(key);
    }

    std::ranges::sort(keys, [](const ChunkKey &a, const ChunkKey &b)
    {
        if (a.level != b.level) return a.level > b.level;
        return std::tie(a.y, a.x) < std::tie(b.y, b.x);
    });

    cover.keys = std::move(keys);
    return cover;
}

std::vector<ChunkKey> ChunkTree::selectVisible(
    const Interval &viewXRange, const Interval &viewYRange, const int targetLevel) const
{
    return selectVisibleCover(viewXRange, viewYRange, targetLevel).keys;
}

std::map<int, size_t> ChunkTree::levelCounts() const
{
    std::map<int, size_t> counts;
    for (const auto &[key, _] : nodes)
    {
        counts[key.level]++;
    }
    return counts;
}
