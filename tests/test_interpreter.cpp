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

// build a literal node straight from a token so we can hand it to evaluate()
// without dragging a whole let-statement through the parser first.
std::unique_ptr<LiteralExpr> literal(TokenKind kind, const std::string& text) {
    return std::make_unique<LiteralExpr>(Token(kind, text, 1, 1));
}

} // namespace

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

TEST_CASE("integer literal evaluates to an int value", "[interp]") {
    Interpreter interp;
    auto node = literal(TokenKind::Integer, "42");
    Value v = interp.evaluate(*node);
    REQUIRE(is_int(v));
    REQUIRE(std::get<int64_t>(v) == 42);
}

TEST_CASE("float literal evaluates to a double value", "[interp]") {
    Interpreter interp;
    auto node = literal(TokenKind::Float, "3.5");
    Value v = interp.evaluate(*node);
    REQUIRE(is_float(v));
    REQUIRE(std::get<double>(v) == 3.5);
}

TEST_CASE("string literal evaluates to its text", "[interp]") {
    Interpreter interp;
    // the lexer hands strings over already unquoted, so the lexeme is the body.
    auto node = literal(TokenKind::String, "hello");
    Value v = interp.evaluate(*node);
    REQUIRE(is_string(v));
    REQUIRE(std::get<std::string>(v) == "hello");
}

TEST_CASE("true and false literals evaluate to bools", "[interp]") {
    Interpreter interp;

    auto yes = literal(TokenKind::True, "true");
    Value tv = interp.evaluate(*yes);
    REQUIRE(is_bool(tv));
    REQUIRE(std::get<bool>(tv) == true);

    auto no = literal(TokenKind::False, "false");
    Value fv = interp.evaluate(*no);
    REQUIRE(is_bool(fv));
    REQUIRE(std::get<bool>(fv) == false);
}

TEST_CASE("nil literal evaluates to nil", "[interp]") {
    Interpreter interp;
    auto node = literal(TokenKind::Nil, "nil");
    Value v = interp.evaluate(*node);
    REQUIRE(is_nil(v));
}
