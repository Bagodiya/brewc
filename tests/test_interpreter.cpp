#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
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

// shorthand for an integer literal node, since most arithmetic tests below only
// ever feed whole numbers in.
std::unique_ptr<LiteralExpr> int_lit(const std::string& text) {
    return literal(TokenKind::Integer, text);
}

// glue two operand nodes together with an operator token so a test can spell out
// `2 + 3` without going through the parser.
std::unique_ptr<BinaryExpr> binary(std::unique_ptr<Expr> lhs, TokenKind op,
                                   const std::string& lexeme, std::unique_ptr<Expr> rhs) {
    return std::make_unique<BinaryExpr>(std::move(lhs), Token(op, lexeme, 1, 1), std::move(rhs));
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

TEST_CASE("integer arithmetic stays an int", "[interp]") {
    Interpreter interp;

    auto sum = binary(int_lit("2"), TokenKind::Plus, "+", int_lit("3"));
    Value v = interp.evaluate(*sum);
    REQUIRE(is_int(v));
    REQUIRE(std::get<int64_t>(v) == 5);

    auto diff = binary(int_lit("10"), TokenKind::Minus, "-", int_lit("4"));
    REQUIRE(std::get<int64_t>(interp.evaluate(*diff)) == 6);

    auto prod = binary(int_lit("6"), TokenKind::Star, "*", int_lit("7"));
    REQUIRE(std::get<int64_t>(interp.evaluate(*prod)) == 42);
}

TEST_CASE("integer division truncates toward zero", "[interp]") {
    Interpreter interp;
    auto quot = binary(int_lit("7"), TokenKind::Slash, "/", int_lit("2"));
    Value v = interp.evaluate(*quot);
    REQUIRE(is_int(v));
    REQUIRE(std::get<int64_t>(v) == 3);
}

TEST_CASE("integer modulo gives the remainder", "[interp]") {
    Interpreter interp;
    auto rem = binary(int_lit("7"), TokenKind::Percent, "%", int_lit("3"));
    REQUIRE(std::get<int64_t>(interp.evaluate(*rem)) == 1);
}

TEST_CASE("float arithmetic stays a float", "[interp]") {
    Interpreter interp;
    auto lhs = literal(TokenKind::Float, "1.5");
    auto rhs = literal(TokenKind::Float, "2.0");
    auto sum = binary(std::move(lhs), TokenKind::Plus, "+", std::move(rhs));
    Value v = interp.evaluate(*sum);
    REQUIRE(is_float(v));
    REQUIRE(std::get<double>(v) == 3.5);
}

TEST_CASE("nested arithmetic respects the tree shape", "[interp]") {
    Interpreter interp;
    // 2 + 3 * 4 built as 2 + (3 * 4), so the result is 14, not 20.
    auto mul = binary(int_lit("3"), TokenKind::Star, "*", int_lit("4"));
    auto expr = binary(int_lit("2"), TokenKind::Plus, "+", std::move(mul));
    REQUIRE(std::get<int64_t>(interp.evaluate(*expr)) == 14);
}

TEST_CASE("dividing an int by zero is an error", "[interp]") {
    Interpreter interp;
    auto bad = binary(int_lit("1"), TokenKind::Slash, "/", int_lit("0"));
    REQUIRE_THROWS_AS(interp.evaluate(*bad), std::runtime_error);
}

TEST_CASE("arithmetic on non-numbers is an error", "[interp]") {
    Interpreter interp;
    auto text = literal(TokenKind::String, "hi");
    auto bad = binary(int_lit("1"), TokenKind::Plus, "+", std::move(text));
    REQUIRE_THROWS_AS(interp.evaluate(*bad), std::runtime_error);
}
