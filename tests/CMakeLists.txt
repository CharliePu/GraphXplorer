add_executable(GraphXplorerTests
        EvaluatorTests.cpp
        FormulaTests.cpp
        GraphGenerationTests.cpp
        ParserTests.cpp
        TokenizerTests.cpp
)

target_link_libraries(GraphXplorerTests PRIVATE
        GraphXplorerLib
        Catch2::Catch2WithMain
)
