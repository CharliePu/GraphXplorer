cmake_minimum_required(VERSION 3.28)
project(GraphXplorer)

find_package(Catch2 3 REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Freetype REQUIRED)

set(CMAKE_CXX_STANDARD 26)

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
        src/Math/GraphProcessor.cpp
        src/Math/GraphProcessor.h
        src/Math/Graph.cpp
        src/Math/Graph.h
        src/Math/Interval.cpp
        src/Math/Interval.h
        src/Math/GraphRasterizer.cpp
        src/Math/GraphRasterizer.h
        src/Util/ThreadPool.cpp
        src/Util/ThreadPool.h
        src/Core/Window.cpp
        src/Core/Window.h
        src/UI/Grid.h
        src/UI/Grid.cpp
        src/UI/AxisLabels.cpp
        src/UI/AxisLabels.h
        src/Render/TextMeshesGenerator.cpp
        src/Render/TextMeshesGenerator.h
        src/UI/InputBox.cpp
        src/UI/InputBox.h
)

target_include_directories(GraphXplorer PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(GraphXplorer PRIVATE glfw glad::glad glm::glm Freetype::Freetype)

# Unit testing
add_executable(GraphXplorerTests
        test/TestFormula.cpp
        src/Math/Formula.cpp
        test/TestComputeEngine.cpp)
target_link_libraries(GraphXplorerTests PRIVATE Catch2::Catch2WithMain)



# Copy fonts and shaders to the build directory
file(GLOB SHADER_FILES "${CMAKE_SOURCE_DIR}/shader/*")
file(GLOB FONT_FILES "${CMAKE_SOURCE_DIR}/font/*")

# Use generator expressions to handle different configurations and generators
add_custom_command(
        TARGET GraphXplorer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:GraphXplorer>/shader
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/shader $<TARGET_FILE_DIR:GraphXplorer>/shader
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:GraphXplorer>/font
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/font $<TARGET_FILE_DIR:GraphXplorer>/font
)

add_custom_target(copy_shaders_and_fonts
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:GraphXplorer>/shader
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/shader $<TARGET_FILE_DIR:GraphXplorer>/shader
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:GraphXplorer>/font
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/font $<TARGET_FILE_DIR:GraphXplorer>/font
        COMMENT "Copying shaders and fonts..."
)
