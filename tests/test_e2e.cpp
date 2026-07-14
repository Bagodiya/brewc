#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "brewc/interpreter.h"
#include "brewc/lexer.h"
#include "brewc/parser.h"
#include "brewc/token.h"

using namespace brewc;

namespace {

// pull the whole token stream out of a source string, the same way the parser
// and interpreter tests do, so an example file travels the exact path the real
// driver would put it through.
std::vector<Token> lex_all(const std::string& source) {
    Lexer lexer(source);
    std::vector<Token> tokens;
    while (true) {
        Token tok = lexer.next_token();
        bool done = tok.kind == TokenKind::End;
        tokens.push_back(std::move(tok));
        if (done) {
            break;
        }
    }
    return tokens;
}

// slurp one of the .brew files under examples/. cmake bakes the directory in as
// BREWC_EXAMPLES_DIR so it doesn't matter what folder ctest is run from.
std::string read_example(const std::string& name) {
    std::string path = std::string(BREWC_EXAMPLES_DIR) + "/" + name;
    std::ifstream file(path);
    REQUIRE(file.is_open());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// run a whole program from source to finish — lex, parse, interpret — and hand
// back everything print wrote out. cout gets swapped for a stringstream while it
// runs and is always put back, even if interpreting throws partway through.
std::string run_source(const std::string& source) {
    Parser parser(lex_all(source));
    auto program = parser.parse_program();
    // a bad example should fail loudly here instead of quietly running only the
    // statements the parser managed to recover.
    REQUIRE(parser.errors().empty());

    Interpreter interp;
    std::ostringstream sink;
    std::streambuf* previous = std::cout.rdbuf(sink.rdbuf());
    try {
        interp.interpret(program);
    } catch (...) {
        std::cout.rdbuf(previous);
        throw;
    }
    std::cout.rdbuf(previous);
    return sink.str();
}

// read an example file and run it in one shot.
std::string run_example(const std::string& name) {
    return run_source(read_example(name));
}

} // namespace

TEST_CASE("hello example prints its greeting", "[e2e]") {
    REQUIRE(run_example("hello.brew") == "hello from brew\n");
}

TEST_CASE("arithmetic example prints one result per line", "[e2e]") {
    // 20 with 6: sum, difference, product, truncating divide, remainder.
    REQUIRE(run_example("arithmetic.brew") == "26\n14\n120\n3\n2\n");
}

TEST_CASE("countdown example recurses down to one", "[e2e]") {
    REQUIRE(run_example("countdown.brew") == "3\n2\n1\n");
}

TEST_CASE("conditionals example takes the branch matching its input", "[e2e]") {
    REQUIRE(run_example("conditionals.brew") == "positive\nzero or less\n");
}
