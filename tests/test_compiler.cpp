#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
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

// compile a lone literal token. there is no expression statement in the grammar,
// so a bare `42` can't be parsed at the top level of a program — the node has to
// be built by hand and fed to compile_expression.
Chunk compile_literal(TokenKind kind, const std::string& text, int line = 1, int column = 1) {
    Compiler compiler;
    LiteralExpr literal(Token(kind, text, line, column));
    return compiler.compile_expression(literal);
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
    // the statement visitors are still empty, so nothing here reaches down into
    // the literals inside them yet. all this really checks is that the walk gets
    // to every node and comes back.
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
    // Const, its pool index, then the Return compile_expression puts on the end.
    Chunk chunk = compile_literal(TokenKind::Integer, "42");
    REQUIRE(chunk.size() == 3);
    REQUIRE(op_at(chunk, 0) == Opcode::Const);
    REQUIRE(op_at(chunk, 2) == Opcode::Return);
}

TEST_CASE("an integer literal lands in the pool with Const pointing at it", "[compiler]") {
    Chunk chunk = compile_literal(TokenKind::Integer, "42");
    REQUIRE(chunk.constants.size() == 1);
    REQUIRE(is_int(chunk.constants[0]));
    REQUIRE(std::get<int64_t>(chunk.constants[0]) == 42);

    // the byte after Const is the index, and this is the only constant so far.
    REQUIRE(chunk.code[1] == 0);
}

TEST_CASE("a float literal keeps its fractional part", "[compiler]") {
    Chunk chunk = compile_literal(TokenKind::Float, "3.5");
    REQUIRE(chunk.constants.size() == 1);
    REQUIRE(is_float(chunk.constants[0]));
    REQUIRE(std::get<double>(chunk.constants[0]) == 3.5);
}

TEST_CASE("a string literal is stored as a string constant", "[compiler]") {
    // the lexer already stripped the quotes and expanded the escapes, so what the
    // token carries is the finished text.
    Chunk chunk = compile_literal(TokenKind::String, "hello");
    REQUIRE(chunk.constants.size() == 1);
    REQUIRE(is_string(chunk.constants[0]));
    REQUIRE(std::get<std::string>(chunk.constants[0]) == "hello");
}

TEST_CASE("true, false and nil use their own opcode and no pool slot", "[compiler]") {
    Chunk yes = compile_literal(TokenKind::True, "true");
    REQUIRE(yes.size() == 2);
    REQUIRE(op_at(yes, 0) == Opcode::True);
    REQUIRE(yes.constants.empty());

    Chunk no = compile_literal(TokenKind::False, "false");
    REQUIRE(op_at(no, 0) == Opcode::False);
    REQUIRE(no.constants.empty());

    Chunk nothing = compile_literal(TokenKind::Nil, "nil");
    REQUIRE(op_at(nothing, 0) == Opcode::Nil);
    REQUIRE(nothing.constants.empty());
}

TEST_CASE("a literal stamps its own source line on the instruction", "[compiler]") {
    Chunk chunk = compile_literal(TokenKind::Integer, "7", 9, 4);
    REQUIRE(chunk.line_at(0) == 9);
    REQUIRE(chunk.line_at(1) == 9);
}

TEST_CASE("the same literal compiled twice does not double up in the pool", "[compiler]") {
    // add_constant already dedupes, but the compiler has to be handing it equal
    // values for that to help — pushing the token text through unconverted would
    // still be one slot per literal.
    Compiler compiler;
    LiteralExpr first(Token(TokenKind::Integer, "5", 1, 1));
    LiteralExpr second(Token(TokenKind::Integer, "5", 2, 1));

    compiler.compile_expression(first);
    Chunk chunk = compiler.compile_expression(second);

    // a fresh chunk each time, so both runs should look identical.
    REQUIRE(chunk.constants.size() == 1);
    REQUIRE(std::get<int64_t>(chunk.constants[0]) == 5);
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
