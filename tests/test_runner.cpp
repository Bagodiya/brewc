#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>

#include "brewc/runner.h"

using namespace brewc;

namespace {

// build the full path to one of the example files. cmake hands us the examples
// directory so this doesn't depend on where ctest happens to be run from.
std::string example_path(const std::string& name) {
    return std::string(BREWC_EXAMPLES_DIR) + "/" + name;
}

// run a file through run_file while grabbing whatever it printed to cout, and
// give back both the exit code and the captured text. cout is always restored,
// even if run_file throws on the way out.
struct RunResult {
    int code;
    std::string output;
};

RunResult run_capturing(const std::string& path) {
    std::ostringstream sink;
    std::streambuf* previous = std::cout.rdbuf(sink.rdbuf());
    int code;
    try {
        code = run_file(path);
    } catch (...) {
        std::cout.rdbuf(previous);
        throw;
    }
    std::cout.rdbuf(previous);
    return {code, sink.str()};
}

} // namespace

TEST_CASE("run_file executes a program and reports success", "[runner]") {
    RunResult result = run_capturing(example_path("hello.brew"));
    REQUIRE(result.code == 0);
    REQUIRE(result.output == "hello from brew\n");
}

TEST_CASE("run_file runs a program with control flow", "[runner]") {
    RunResult result = run_capturing(example_path("fizzbuzz.brew"));
    REQUIRE(result.code == 0);
    REQUIRE(result.output ==
            "1\n2\nfizz\n4\nbuzz\nfizz\n7\n8\nfizz\nbuzz\n11\nfizz\n13\n14\nfizzbuzz\n");
}

TEST_CASE("run_file fails when the file is missing", "[runner]") {
    RunResult result = run_capturing(example_path("does_not_exist.brew"));
    REQUIRE(result.code == 1);
    // the program itself never runs, so nothing lands on cout.
    REQUIRE(result.output.empty());
}
