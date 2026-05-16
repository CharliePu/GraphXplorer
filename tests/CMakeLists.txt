add_executable(GraphXplorerTests
        ArchitectureBoundaryTests.cpp
        AsyncFrameInboxTests.cpp
        ContractTests.cpp
        FormulaCompilerTests.cpp
        EvaluatorTests.cpp
        FramePipelineTests.cpp
        FormulaTests.cpp
        InputScenarioRunnerTests.cpp
        InteractionControllerTests.cpp
        ParserTests.cpp
        RenderCommandTests.cpp
        TilePlannerBackendTests.cpp
        VisualCoverBuilderTests.cpp
        TileCacheTests.cpp
        TrigSupportTests.cpp
        TokenizerTests.cpp
)

target_include_directories(GraphXplorerTests PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(GraphXplorerTests PRIVATE
        GraphXplorerApp
        GraphXplorerUI
        GraphXplorerCompute
        GraphXplorerRender
        GraphXplorerTile
        GraphXplorerFormula
        GraphXplorerUtil
        Catch2::Catch2WithMain
)
