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
    ${CMAKE_SOURCE_DIR}/src/lexer.cpp
    ${CMAKE_SOURCE_DIR}/src/parser.cpp
    ${CMAKE_SOURCE_DIR}/src/ast_printer.cpp
)

target_link_libraries(brewc_tests PRIVATE Catch2::Catch2WithMain)
target_include_directories(brewc_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(brewc_tests)
