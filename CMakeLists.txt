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
        src/Tile/TileCache.cpp
        src/Tile/TileCache.h
        src/Tile/TileMath.h
)
target_link_libraries(GraphXplorerTile PUBLIC GraphXplorerFormula GraphXplorerUtil)

add_library(GraphXplorerCompute
        src/Compute/BatchOptimizer.cpp
        src/Compute/BatchOptimizer.h
        src/Compute/ComputeBackend.h
        src/Compute/CpuComputeBackend.cpp
        src/Compute/InequalityTileRefiner.cpp
        src/Compute/InequalityTileRefiner.h
        src/Compute/OpenClComputeBackend.cpp
        src/Compute/TileJob.h
        src/Compute/TilePlanner.cpp
        src/Compute/TilePlanner.h
        src/Compute/TileRuntime.cpp
        src/Compute/TileRuntime.h
        src/Compute/VisualCoverBuilder.cpp
        src/Compute/VisualCoverBuilder.h
)
target_include_directories(GraphXplorerCompute PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(GraphXplorerCompute PUBLIC GraphXplorerFormula GraphXplorerTile GraphXplorerUtil glfw)

add_library(GraphXplorerRender
        src/Render/FrameCommandBuffer.cpp
        src/Render/FrameCommandBuffer.h
        src/Render/Renderer.cpp
        src/Render/Renderer.h
        src/Render/RenderResourceManager.cpp
        src/Render/RenderResourceManager.h
        src/Render/TextAtlas.h
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
)
target_include_directories(GraphXplorerUI PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(GraphXplorerUI PUBLIC glfw)

add_library(GraphXplorerApp
        src/App/AppState.cpp
        src/App/AppState.h
        src/App/FrameBudgetController.cpp
        src/App/FrameBudgetController.h
        src/App/FramePipeline.cpp
        src/App/FramePipeline.h
        src/App/InteractionController.cpp
        src/App/InteractionController.h
        src/App/PresentationPlanner.cpp
        src/App/PresentationPlanner.h
        src/App/UiLayout.cpp
        src/App/UiLayout.h
        src/Core/Application.cpp
        src/Core/Application.h
        src/Core/MainThreadRequestMailbox.cpp
        src/Core/MainThreadRequestMailbox.h
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
