add_executable(GraphXplorerTests
        AsyncFrameInboxTests.cpp
        EvaluatorTests.cpp
        FormulaTests.cpp
        GraphGenerationTests.cpp
        InputScenarioRunnerTests.cpp
        ParserTests.cpp
        TrigSupportTests.cpp
        TokenizerTests.cpp
)

target_link_libraries(GraphXplorerTests PRIVATE
        GraphXplorerLib
        Catch2::Catch2WithMain
)
