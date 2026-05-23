add_executable(GraphXplorerTests
        ArchitectureBoundaryTests.cpp
        AsyncFrameInboxTests.cpp
        BatchOptimizerTests.cpp
        ContractTests.cpp
        FormulaCompilerTests.cpp
        EvaluatorTests.cpp
        FrameBudgetControllerTests.cpp
        FramePipelineTests.cpp
        FormulaTests.cpp
        InputScenarioRunnerTests.cpp
        InteractionControllerTests.cpp
        MainThreadRequestMailboxTests.cpp
        ParserTests.cpp
        PresentationPlannerTests.cpp
        RenderCommandTests.cpp
        ThreadPoolTests.cpp
        TilePlannerBackendTests.cpp
        TileMathTests.cpp
        VisualCoverBuilderTests.cpp
        TileCacheTests.cpp
        TileRuntimeTests.cpp
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

add_executable(GraphXplorerCpuBench
        CpuPathBenchmark.cpp
)

target_include_directories(GraphXplorerCpuBench PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(GraphXplorerCpuBench PRIVATE
        GraphXplorerCompute
        GraphXplorerTile
        GraphXplorerFormula
        GraphXplorerUtil
)
