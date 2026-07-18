include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

add_executable(brewc_tests
    test_smoke.cpp
    test_lexer.cpp
    test_ast.cpp
    test_parser.cpp
    test_ast_printer.cpp
    test_value.cpp
    test_environment.cpp
    test_interpreter.cpp
    test_runtime_error.cpp
    test_e2e.cpp
    test_runner.cpp
    ${CMAKE_SOURCE_DIR}/src/lexer.cpp
    ${CMAKE_SOURCE_DIR}/src/parser.cpp
    ${CMAKE_SOURCE_DIR}/src/ast_printer.cpp
    ${CMAKE_SOURCE_DIR}/src/value.cpp
    ${CMAKE_SOURCE_DIR}/src/environment.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime_error.cpp
    ${CMAKE_SOURCE_DIR}/src/interpreter.cpp
    ${CMAKE_SOURCE_DIR}/src/runner.cpp
)

target_link_libraries(brewc_tests PRIVATE Catch2::Catch2WithMain)
target_include_directories(brewc_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)

# the end-to-end tests read real .brew files off disk, so hand them the path to
# the examples folder instead of guessing it from the working directory.
target_compile_definitions(brewc_tests PRIVATE
    BREWC_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(brewc_tests)
