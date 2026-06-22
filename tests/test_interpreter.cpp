#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "brewc/interpreter.h"
#include "brewc/lexer.h"
#include "brewc/parser.h"
#include "brewc/token.h"

using namespace brewc;

namespace {

// same lex-everything helper the parser tests use, so the interpreter gets the
// exact token list the real driver hands it.
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

std::vector<std::unique_ptr<Stmt>> parse_program(const std::string& source) {
    Parser parser(lex_all(source));
    return parser.parse_program();
}

} // namespace

// the visit_* bodies are still stubs at this step, so there is no observable
// result to check yet. for now just pin down that the scaffold links and that
// running a program drives every statement without falling over. the real
// behaviour checks land alongside the evaluation steps that follow.

TEST_CASE("interpreting an empty program is a no-op", "[interp]") {
    Interpreter interp;
    std::vector<std::unique_ptr<Stmt>> program;
    REQUIRE_NOTHROW(interp.interpret(program));
}

TEST_CASE("interpreter walks a parsed program without error", "[interp]") {
    auto program = parse_program("let x = 1 + 2;");
    REQUIRE_FALSE(program.empty());

    Interpreter interp;
    REQUIRE_NOTHROW(interp.interpret(program));
}
