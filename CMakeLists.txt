cmake_minimum_required(VERSION 3.28)
project(GraphXplorer)

find_package(Catch2 3 REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 20)

add_executable(GraphXplorer src/main.cpp
        src/Core/Application.cpp
        src/Core/Application.h
        src/Core/Input.cpp
        src/Core/Input.h
        src/Render/Renderer.cpp
        src/Render/Renderer.h
        src/Render/Mesh.cpp
        src/Render/Mesh.h
        src/UI/Plot.cpp
        src/UI/Plot.h
        src/UI/UIComponent.cpp
        src/UI/UIComponent.h
        src/Scene/SceneManager.cpp
        src/Scene/SceneManager.h
        src/Scene/Scene.cpp
        src/Scene/Scene.h
        src/Scene/MainScene.cpp
        src/Scene/MainScene.h
        src/UI/ConsoleInput.cpp
        src/UI/ConsoleInput.h
        src/Math/Formula.cpp
        src/Math/Formula.h
        src/Math/ComputeEngine.cpp
        src/Math/ComputeEngine.h
        src/Math/Graph.cpp
        src/Math/Graph.h
        src/Math/Interval.cpp
        src/Math/Interval.h
        src/Render/GraphRasterizer.cpp
        src/Render/GraphRasterizer.h
        src/Util/ThreadPool.cpp
        src/Util/ThreadPool.h
        src/Core/Window.cpp
        src/Core/Window.h
        src/UI/Grid.h
)

target_include_directories(GraphXplorer PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(GraphXplorer PRIVATE glfw glad::glad glm::glm)

# Unit testing
add_executable(GraphXplorerTests
        test/TestFormula.cpp
        src/Math/Formula.cpp)
target_link_libraries(GraphXplorerTests PRIVATE Catch2::Catch2WithMain)