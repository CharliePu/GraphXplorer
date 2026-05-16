#include "catch.hpp"

#include <algorithm>

#include "../src/Graph/ChunkTree.h"

namespace
{
RasterChunk makeChunk(const int64_t x, const int64_t y, const int level, const int state)
{
    return {
        x,
        y,
        level,
        chunkIndexToRange(x, level),
        chunkIndexToRange(y, level),
        state,
        RasterChunkSource::Exact
    };
}
}

TEST_CASE("ChunkTree links higher levels as parents", "[ChunkTree]")
{
    ChunkTree tree;
    tree.insert(makeChunk(0, 0, 1, -1));
    tree.insert(makeChunk(1, 0, 0, -1));

    const auto *parent = tree.find({0, 0, 1});
    const auto *child = tree.find({1, 0, 0});

    REQUIRE(parent != nullptr);
    REQUIRE(child != nullptr);
    REQUIRE(child->parent == parent);
    CHECK(parent->children[1] == child);
}

TEST_CASE("ChunkTree keeps descendants when a uniform parent arrives", "[ChunkTree]")
{
    ChunkTree tree;
    tree.insert(makeChunk(0, 0, 0, -1));
    tree.insert(makeChunk(0, 0, 1, 1));

    const auto *parent = tree.find({0, 0, 1});
    const auto *child = tree.find({0, 0, 0});

    REQUIRE(parent != nullptr);
    REQUIRE(child != nullptr);
    CHECK(child->parent == parent);
    CHECK(tree.size() == 2);
}

TEST_CASE("ChunkTree replacing a mixed parent keeps descendants safely", "[ChunkTree]")
{
    ChunkTree tree;
    tree.insert(makeChunk(0, 0, 1, -1));
    tree.insert(makeChunk(0, 0, 0, -1));
    tree.insert(makeChunk(1, 0, 0, -1));
    tree.insert(makeChunk(0, 0, 1, 0));

    const auto *parent = tree.find({0, 0, 1});
    const auto *leftChild = tree.find({0, 0, 0});
    const auto *rightChild = tree.find({1, 0, 0});

    REQUIRE(parent != nullptr);
    REQUIRE(leftChild != nullptr);
    REQUIRE(rightChild != nullptr);
    CHECK(leftChild->parent == parent);
    CHECK(rightChild->parent == parent);
    CHECK(tree.size() == 3);
}

TEST_CASE("ChunkTree rejects enormous visible target grids", "[ChunkTree]")
{
    ChunkTree tree;
    tree.insert(makeChunk(0, 0, 30, 1));

    const auto cover = tree.selectVisibleCover(
        Interval{-1'000'000.0, 1'000'000.0},
        Interval{-1'000'000.0, 1'000'000.0},
        MIN_CHUNK_LEVEL);

    CHECK_FALSE(cover.bounded);
    CHECK(cover.keys.empty());
    CHECK(cover.missingCells.empty());
}

TEST_CASE("ChunkTree visible cover reports missing target cells", "[ChunkTree]")
{
    ChunkTree tree;
    tree.insert(makeChunk(0, 0, 0, 1));

    const auto cover = tree.selectVisibleCover(Interval{0.0, 2.0}, Interval{0.0, 1.0}, 0);

    CHECK(cover.bounded);
    CHECK_FALSE(cover.complete());
    CHECK(cover.targetCellCount == 2);
    REQUIRE(cover.keys.size() == 1);
    CHECK(cover.keys.front() == ChunkKey{0, 0, 0});
    REQUIRE(cover.missingCells.size() == 1);
    CHECK(cover.missingCells.front().x == 1);
    CHECK(cover.missingCells.front().y == 0);
    CHECK(cover.missingCells.front().level == 0);
}

TEST_CASE("ChunkTree visible cover treats known false chunks as covered", "[ChunkTree]")
{
    ChunkTree tree;
    tree.insert(makeChunk(0, 0, 0, 0));

    const auto cover = tree.selectVisibleCover(Interval{0.0, 1.0}, Interval{0.0, 1.0}, 0);

    CHECK(cover.bounded);
    CHECK(cover.complete());
    CHECK(cover.missingCells.empty());
    REQUIRE(cover.keys.size() == 1);
    CHECK(cover.keys.front() == ChunkKey{0, 0, 0});
}

TEST_CASE("ChunkTree fine child does not fully cover coarser target cell", "[ChunkTree]")
{
    ChunkTree tree;
    tree.insert(makeChunk(0, 0, 0, 1));

    const auto cover = tree.selectVisibleCover(Interval{0.0, 2.0}, Interval{0.0, 2.0}, 1);

    CHECK(cover.bounded);
    CHECK_FALSE(cover.complete());
    CHECK(cover.keys.empty());
    REQUIRE(cover.missingCells.size() == 1);
    CHECK(cover.missingCells.front().x == 0);
    CHECK(cover.missingCells.front().y == 0);
    CHECK(cover.missingCells.front().level == 1);
}

TEST_CASE("ChunkTree complete fine descendants can cover coarser target cell", "[ChunkTree]")
{
    ChunkTree tree;
    tree.insert(makeChunk(0, 0, 0, 1));
    tree.insert(makeChunk(1, 0, 0, 1));
    tree.insert(makeChunk(0, 1, 0, 1));
    tree.insert(makeChunk(1, 1, 0, 1));

    const auto cover = tree.selectVisibleCover(Interval{0.0, 2.0}, Interval{0.0, 2.0}, 1);

    CHECK(cover.bounded);
    CHECK(cover.complete());
    CHECK(cover.missingCells.empty());
    CHECK(cover.keys.size() == 4);
}

TEST_CASE("ChunkTree visible cover keeps exact child when parent also exists", "[ChunkTree]")
{
    ChunkTree tree;
    tree.insert(makeChunk(0, 0, 1, -1));
    tree.insert(makeChunk(0, 0, 0, 1));

    const auto cover = tree.selectVisibleCover(Interval{0.0, 1.0}, Interval{0.0, 1.0}, 0);

    CHECK(cover.bounded);
    CHECK(cover.complete());
    CHECK(std::ranges::find(cover.keys, ChunkKey{0, 0, 0}) != cover.keys.end());
}
