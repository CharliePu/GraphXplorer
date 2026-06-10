cmake_minimum_required(VERSION 3.28)
project(GraphXplorer)

set(CMAKE_CXX_STANDARD 23)

find_package(Catch2 3 REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(Freetype REQUIRED)

enable_testing()

# The CPU rendering engine and GL app (namespace gxr). Contract in CLAUDE.md,
# module map in engine/README.md, design in docs/ARCHITECTURE_REWRITE.md.
add_subdirectory(engine)
