cmake_minimum_required(VERSION 3.28)
project(GraphXplorer)

find_package(Catch2 3 REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(Microsoft.GSL CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 23)

add_library(GraphXplorerUtil
        src/Math/Interval.cpp
        src/Math/Interval.h
        src/Util/AsyncFrameInbox.h
        src/Util/Contracts.h
        src/Util/InputScenarioRunner.cpp
        src/Util/InputScenarioRunner.h
        src/Util/PerformanceProfiler.h
        src/Util/PipelineLog.h
        src/Util/StableHash.h
        src/Util/ThreadPool.cpp
        src/Util/ThreadPool.h
)

add_library(GraphXplorerFormula
        src/Formula/Evaluator.cpp
        src/Formula/Evaluator.h
        src/Formula/Formula.cpp
        src/Formula/Formula.h
        src/Formula/FormulaCompiler.cpp
        src/Formula/FormulaCompiler.h
        src/Formula/Parser.cpp
        src/Formula/Parser.h
        src/Formula/Token.h
        src/Formula/Tokenizer.cpp
        src/Formula/Tokenizer.h
)
target_link_libraries(GraphXplorerFormula PUBLIC GraphXplorerUtil)

add_library(GraphXplorerTile
        src/Graph/ChunkTree.cpp
        src/Graph/ChunkTree.h
        src/Graph/Graph.h
        src/Math/RasterizedPlot.h
        src/Tile/TileCache.cpp
        src/Tile/TileCache.h
        src/Tile/TileMath.h
)
target_link_libraries(GraphXplorerTile PUBLIC GraphXplorerFormula GraphXplorerUtil)

add_library(GraphXplorerCompute
        src/Compute/ComputeBackend.h
        src/Compute/CpuComputeBackend.cpp
        src/Compute/TileScheduler.cpp
        src/Compute/TileScheduler.h
        src/Math/ChunkRenderer.h
        src/Math/CpuChunkRenderer.cpp
        src/Math/CpuChunkRenderer.h
        src/Math/ChunkRegionRasterizer.cpp
        src/Math/ChunkRegionRasterizer.h
        src/Math/ChunkContourRasterizer.cpp
        src/Math/ChunkContourRasterizer.h
        src/Math/ComputeEngine.cpp
        src/Math/ComputeEngine.h
        src/Math/GraphProcessor.cpp
        src/Math/GraphProcessor.h
        src/Math/GraphRasterizer.cpp
        src/Math/GraphRasterizer.h
        src/Math/OpenCLChunkRenderer.cpp
        src/Math/OpenCLChunkRenderer.h
)
target_include_directories(GraphXplorerCompute PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(GraphXplorerCompute PUBLIC GraphXplorerFormula GraphXplorerTile GraphXplorerUtil glfw)

add_library(GraphXplorerRender
        src/Render/FrameCommandBuffer.cpp
        src/Render/FrameCommandBuffer.h
        src/Render/Mesh.cpp
        src/Render/Mesh.h
        src/Render/Renderer.cpp
        src/Render/Renderer.h
        src/Render/RenderResourceManager.cpp
        src/Render/RenderResourceManager.h
        src/Render/TextAtlas.h
        src/Render/TextMeshesGenerator.cpp
        src/Render/TextMeshesGenerator.h
        src/Render/UploadPlanner.cpp
        src/Render/UploadPlanner.h
)
target_include_directories(GraphXplorerRender PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(GraphXplorerRender PUBLIC GraphXplorerUtil GraphXplorerTile glfw glad::glad glm::glm Freetype::Freetype)

add_library(GraphXplorerUI
        src/Core/Input.cpp
        src/Core/Input.h
        src/Core/Window.cpp
        src/Core/Window.h
        src/UI/AxisLabels.cpp
        src/UI/AxisLabels.h
        src/UI/ConsoleInput.cpp
        src/UI/ConsoleInput.h
        src/UI/Grid.cpp
        src/UI/Grid.h
        src/UI/InputBox.cpp
        src/UI/InputBox.h
        src/UI/Plot.cpp
        src/UI/Plot.h
        src/UI/UIComponent.cpp
        src/UI/UIComponent.h
)
target_include_directories(GraphXplorerUI PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(GraphXplorerUI PUBLIC GraphXplorerCompute GraphXplorerRender glfw glad::glad glm::glm)

add_library(GraphXplorerApp
        src/App/AppState.cpp
        src/App/AppState.h
        src/App/FramePipeline.cpp
        src/App/FramePipeline.h
        src/Core/Application.cpp
        src/Core/Application.h
        src/Scene/MainScene.cpp
        src/Scene/MainScene.h
        src/Scene/Scene.cpp
        src/Scene/Scene.h
        src/Scene/SceneManager.cpp
        src/Scene/SceneManager.h
)
target_include_directories(GraphXplorerApp PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(GraphXplorerApp PUBLIC GraphXplorerUI GraphXplorerCompute GraphXplorerRender GraphXplorerFormula GraphXplorerTile GraphXplorerUtil glfw glad::glad glm::glm Freetype::Freetype)


add_executable(GraphXplorer src/main.cpp)

target_include_directories(GraphXplorer PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(GraphXplorer PRIVATE glfw GraphXplorerApp)



# Unit testing
add_subdirectory(tests)



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
