#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "brewc/ast.h"
#include "brewc/chunk.h"
#include "brewc/compiler.h"
#include "brewc/lexer.h"
#include "brewc/parser.h"
#include "brewc/token.h"

using namespace brewc;

namespace {

// the same lex-everything helper the parser and interpreter tests use, so the
// compiler is fed the exact token list the real driver would give it.
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

// parse and insist it worked. the parser recovers from errors instead of
// throwing, so a typo in a test source string would otherwise hand back an empty
// program that compiles to a bare Return — which is exactly what half the checks
// below expect, so the test would pass without compiling anything.
std::vector<std::unique_ptr<Stmt>> parse_program(const std::string& source) {
    Parser parser(lex_all(source));
    std::vector<std::unique_ptr<Stmt>> program = parser.parse_program();
    REQUIRE(parser.errors().empty());
    return program;
}

// source straight through to bytecode, which is what most tests here want.
Chunk compile_source(const std::string& source) {
    std::vector<std::unique_ptr<Stmt>> program = parse_program(source);
    Compiler compiler;
    return compiler.compile(program);
}

// read one byte back as an opcode. the chunk stores plain bytes, so a test
// comparing against Opcode::Return has to cast somewhere.
Opcode op_at(const Chunk& chunk, std::size_t offset) {
    return static_cast<Opcode>(chunk.code[offset]);
}

} // namespace

TEST_CASE("an empty program compiles to a single Return", "[compiler]") {
    Chunk chunk = compile_source("");
    REQUIRE(chunk.size() == 1);
    REQUIRE(op_at(chunk, 0) == Opcode::Return);
}

TEST_CASE("the trailing Return gets a line recorded for it", "[compiler]") {
    // a runtime error on the last instruction still has to report somewhere, so
    // no byte is allowed into the chunk without a line beside it.
    Chunk chunk = compile_source("");
    REQUIRE(chunk.lines.size() == chunk.code.size());
    REQUIRE(chunk.line_at(0) == 1);
}

TEST_CASE("a fresh chunk starts with an empty constant pool", "[compiler]") {
    Chunk chunk = compile_source("");
    REQUIRE(chunk.constants.empty());
}

TEST_CASE("statements compile without the stubs blowing up", "[compiler]") {
    // the visit_* bodies are empty until the later steps, so all this really
    // checks is that the walk reaches every node and comes back. once step 65
    // lands these stop being one byte long.
    REQUIRE(compile_source("let x = 1").size() == 1);
    REQUIRE(compile_source("if 1 < 2 { let y = 3 }").size() == 1);
    REQUIRE(compile_source("while 1 { let z = 2 }").size() == 1);
    REQUIRE(compile_source("fn add(a, b) { let c = a + b }").size() == 1);
}

TEST_CASE("nested blocks are walked all the way down", "[compiler]") {
    Chunk chunk = compile_source("fn outer(n) { while n { if n { let deep = 1 } } }");
    REQUIRE(chunk.size() == 1);
    REQUIRE(op_at(chunk, 0) == Opcode::Return);
}

TEST_CASE("a single expression can be compiled on its own", "[compiler]") {
    Compiler compiler;
    LiteralExpr literal(Token(TokenKind::Integer, "42", 1, 1));
    Chunk chunk = compiler.compile_expression(literal);
    REQUIRE(chunk.size() == 1);
    REQUIRE(op_at(chunk, 0) == Opcode::Return);
}

TEST_CASE("the same compiler can run twice without carrying bytes over", "[compiler]") {
    // the chunk is moved out at the end of compile(), so the second run has to
    // start from a fresh one rather than whatever the move left behind.
    Compiler compiler;
    std::vector<std::unique_ptr<Stmt>> first = parse_program("let a = 1");
    std::vector<std::unique_ptr<Stmt>> second = parse_program("let b = 2");

    Chunk one = compiler.compile(first);
    Chunk two = compiler.compile(second);

    REQUIRE(one.size() == 1);
    REQUIRE(two.size() == 1);
}

TEST_CASE("a compile error says which line and column it came from", "[compiler]") {
    CompileError err(4, 9, "too many constants in one chunk");
    REQUIRE(err.line() == 4);
    REQUIRE(err.column() == 9);
    REQUIRE(std::string(err.what()) == "line 4:9: too many constants in one chunk");
}
