cmake_minimum_required(VERSION 3.16)
project(brewc
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_executable(brewc
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/ast_printer.cpp
    src/value.cpp
    src/environment.cpp
    src/runtime_error.cpp
    src/source_snippet.cpp
    src/interpreter.cpp
    src/repl.cpp
    src/runner.cpp
)
target_include_directories(brewc PRIVATE include)

# keep the version in one place. cmake owns it and the binary just prints
# whatever it was built with, so the two can't drift apart.
target_compile_definitions(brewc PRIVATE BREWC_VERSION="${PROJECT_VERSION}")

enable_testing()
add_subdirectory(tests)