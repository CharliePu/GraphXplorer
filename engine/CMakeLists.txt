# Fresh CPU rendering engine (namespace gxr). The core library is self-contained
# (no legacy src/ target, no GL). The GUI executable adds the GL presenter.

# Core library sources: everything except the GL presenter and the GUI main.
file(GLOB_RECURSE GXR_ENGINE_SRC CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/math/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/expr/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/solve/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tile/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/sched/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/image/*.cpp)
# app/ contains both the library facade (Engine.cpp) and the GUI main; only the
# facade belongs in the library.
list(APPEND GXR_ENGINE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/app/Engine.cpp)

add_library(GxEngine ${GXR_ENGINE_SRC})
target_include_directories(GxEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(GxEngine PUBLIC cxx_std_23)
find_package(Threads REQUIRED)
target_link_libraries(GxEngine PUBLIC Threads::Threads)

# Unit tests + headless proofs
file(GLOB GXR_TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
if(GXR_TEST_SRC)
    add_executable(GxEngineTests ${GXR_TEST_SRC})
    target_link_libraries(GxEngineTests PRIVATE GxEngine Catch2::Catch2WithMain)
endif()

# Headless CPU tools (validatable in any environment)
add_executable(gxrender ${CMAKE_CURRENT_SOURCE_DIR}/tools/render_png.cpp)
target_link_libraries(gxrender PRIVATE GxEngine)
add_executable(gxbench ${CMAKE_CURRENT_SOURCE_DIR}/tools/bench.cpp)
target_link_libraries(gxbench PRIVATE GxEngine)

# Live GUI app (OpenGL). Reuses the project's glfw/glad/glfwpp; Vulkan-ready seam
# is the Presenter interface.
find_package(Freetype REQUIRED)
add_executable(GraphXplorer2
        ${CMAKE_CURRENT_SOURCE_DIR}/app/main_gui.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/present/GlPresenter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/present/Overlay.cpp)
target_include_directories(GraphXplorer2 PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(GraphXplorer2 PRIVATE GXR_ASSET_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(GraphXplorer2 PRIVATE GxEngine glfw glad::glad Freetype::Freetype)
