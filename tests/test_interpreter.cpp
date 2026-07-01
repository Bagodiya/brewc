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

// a let statement we can drop straight into an if branch. it binds `name` to a
// single int literal so a test can run the interpreter and then check whether
// that name made it into scope, which tells us which branch actually fired.
std::unique_ptr<LetStmt> let_int(const std::string& name, const std::string& value) {
    return std::make_unique<LetStmt>(Token(TokenKind::Identifier, name, 1, 1), int_lit(value));
}

// a bare identifier node, so a test can spell out reads like `i` inside a
// hand-built condition or expression without routing through the parser.
std::unique_ptr<IdentifierExpr> ident(const std::string& name) {
    return std::make_unique<IdentifierExpr>(Token(TokenKind::Identifier, name, 1, 1));
}

// did `name` end up bound in the interpreter's scope? reading an unbound name
// throws, so a clean read means it's there. the if tests use this to tell apart
// "then ran", "else ran", and "neither ran".
bool is_bound(Interpreter& interp, const std::string& name) {
    IdentifierExpr id(Token(TokenKind::Identifier, name, 1, 1));
    try {
        interp.evaluate(id);
        return true;
    } catch (const std::runtime_error&) {
        return false;
    }
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

TEST_CASE("less-than on ints gives a bool", "[interp]") {
    Interpreter interp;

    auto lt = binary(int_lit("2"), TokenKind::Less, "<", int_lit("5"));
    Value v = interp.evaluate(*lt);
    REQUIRE(is_bool(v));
    REQUIRE(std::get<bool>(v) == true);

    auto notlt = binary(int_lit("5"), TokenKind::Less, "<", int_lit("2"));
    REQUIRE(std::get<bool>(interp.evaluate(*notlt)) == false);
}

TEST_CASE("the rest of the relational operators on ints", "[interp]") {
    Interpreter interp;

    auto gt = binary(int_lit("9"), TokenKind::Greater, ">", int_lit("4"));
    REQUIRE(std::get<bool>(interp.evaluate(*gt)) == true);

    auto le = binary(int_lit("3"), TokenKind::LessEqual, "<=", int_lit("3"));
    REQUIRE(std::get<bool>(interp.evaluate(*le)) == true);

    auto ge = binary(int_lit("2"), TokenKind::GreaterEqual, ">=", int_lit("8"));
    REQUIRE(std::get<bool>(interp.evaluate(*ge)) == false);
}

TEST_CASE("equality and inequality on ints", "[interp]") {
    Interpreter interp;

    auto eq = binary(int_lit("7"), TokenKind::EqualEqual, "==", int_lit("7"));
    REQUIRE(std::get<bool>(interp.evaluate(*eq)) == true);

    auto ne = binary(int_lit("7"), TokenKind::BangEqual, "!=", int_lit("7"));
    REQUIRE(std::get<bool>(interp.evaluate(*ne)) == false);
}

TEST_CASE("comparing floats works the same way", "[interp]") {
    Interpreter interp;
    auto lhs = literal(TokenKind::Float, "1.5");
    auto rhs = literal(TokenKind::Float, "2.5");
    auto lt = binary(std::move(lhs), TokenKind::Less, "<", std::move(rhs));
    Value v = interp.evaluate(*lt);
    REQUIRE(is_bool(v));
    REQUIRE(std::get<bool>(v) == true);
}

TEST_CASE("strings compare by value with == and !=", "[interp]") {
    Interpreter interp;

    auto same = binary(literal(TokenKind::String, "hi"), TokenKind::EqualEqual, "==",
                       literal(TokenKind::String, "hi"));
    REQUIRE(std::get<bool>(interp.evaluate(*same)) == true);

    auto diff = binary(literal(TokenKind::String, "hi"), TokenKind::BangEqual, "!=",
                       literal(TokenKind::String, "bye"));
    REQUIRE(std::get<bool>(interp.evaluate(*diff)) == true);
}

TEST_CASE("bools compare for equality", "[interp]") {
    Interpreter interp;
    auto eq = binary(literal(TokenKind::True, "true"), TokenKind::EqualEqual, "==",
                     literal(TokenKind::False, "false"));
    REQUIRE(std::get<bool>(interp.evaluate(*eq)) == false);
}

TEST_CASE("values of different types are never equal", "[interp]") {
    Interpreter interp;
    auto eq = binary(int_lit("1"), TokenKind::EqualEqual, "==",
                     literal(TokenKind::String, "1"));
    REQUIRE(std::get<bool>(interp.evaluate(*eq)) == false);
}

TEST_CASE("ordering values that can't be ordered is an error", "[interp]") {
    Interpreter interp;
    auto bad = binary(literal(TokenKind::String, "a"), TokenKind::Less, "<",
                      literal(TokenKind::String, "b"));
    REQUIRE_THROWS_AS(interp.evaluate(*bad), std::runtime_error);
}

TEST_CASE("a let binding can be read back by name", "[interp]") {
    Interpreter interp;
    auto program = parse_program("let x = 41 + 1;");
    interp.interpret(program);

    // the name now lives in the interpreter's scope, so evaluating a bare
    // identifier should hand the bound value straight back.
    IdentifierExpr x(Token(TokenKind::Identifier, "x", 1, 1));
    Value v = interp.evaluate(x);
    REQUIRE(is_int(v));
    REQUIRE(std::get<int64_t>(v) == 42);
}

TEST_CASE("one let can refer to an earlier one", "[interp]") {
    Interpreter interp;
    auto program = parse_program("let a = 10; let b = a + 5;");
    interp.interpret(program);

    IdentifierExpr b(Token(TokenKind::Identifier, "b", 1, 1));
    REQUIRE(std::get<int64_t>(interp.evaluate(b)) == 15);
}

TEST_CASE("redefining a name replaces the old value", "[interp]") {
    Interpreter interp;
    auto program = parse_program("let n = 1; let n = 2;");
    interp.interpret(program);

    IdentifierExpr n(Token(TokenKind::Identifier, "n", 1, 1));
    REQUIRE(std::get<int64_t>(interp.evaluate(n)) == 2);
}

TEST_CASE("reading an unbound name is an error", "[interp]") {
    Interpreter interp;
    IdentifierExpr missing(Token(TokenKind::Identifier, "nope", 1, 1));
    REQUIRE_THROWS_AS(interp.evaluate(missing), std::runtime_error);
}

// wrap a single statement in a one-element program so we can push it through the
// public interpret() entry point instead of reaching for execute().
void run_one(Interpreter& interp, std::unique_ptr<Stmt> stmt) {
    std::vector<std::unique_ptr<Stmt>> program;
    program.push_back(std::move(stmt));
    interp.interpret(program);
}

TEST_CASE("if with a true condition takes the then branch", "[interp]") {
    Interpreter interp;
    auto stmt = std::make_unique<IfStmt>(literal(TokenKind::True, "true"),
                                         let_int("taken", "1"), let_int("skipped", "2"));
    run_one(interp, std::move(stmt));

    REQUIRE(is_bound(interp, "taken"));
    REQUIRE(!is_bound(interp, "skipped"));
}

TEST_CASE("if with a false condition takes the else branch", "[interp]") {
    Interpreter interp;
    auto stmt = std::make_unique<IfStmt>(literal(TokenKind::False, "false"),
                                         let_int("taken", "1"), let_int("other", "2"));
    run_one(interp, std::move(stmt));

    REQUIRE(!is_bound(interp, "taken"));
    REQUIRE(is_bound(interp, "other"));
}

TEST_CASE("a false condition with no else does nothing", "[interp]") {
    Interpreter interp;
    auto stmt = std::make_unique<IfStmt>(literal(TokenKind::False, "false"),
                                         let_int("body", "1"), nullptr);
    run_one(interp, std::move(stmt));

    REQUIRE(!is_bound(interp, "body"));
}

TEST_CASE("a non-bool condition is truthy", "[interp]") {
    Interpreter interp;
    // an int that isn't zero still counts as true, so the then branch runs.
    auto stmt = std::make_unique<IfStmt>(int_lit("7"), let_int("ran", "1"), nullptr);
    run_one(interp, std::move(stmt));

    REQUIRE(is_bound(interp, "ran"));
}

TEST_CASE("nil is falsy", "[interp]") {
    Interpreter interp;
    auto stmt = std::make_unique<IfStmt>(literal(TokenKind::Nil, "nil"),
                                         let_int("ran", "1"), let_int("fallback", "2"));
    run_one(interp, std::move(stmt));

    REQUIRE(!is_bound(interp, "ran"));
    REQUIRE(is_bound(interp, "fallback"));
}

TEST_CASE("a while whose condition starts false never runs its body", "[interp]") {
    Interpreter interp;
    // condition is a plain false literal, so the loop should skip the body outright
    // and "body" never makes it into scope.
    auto stmt = std::make_unique<WhileStmt>(literal(TokenKind::False, "false"),
                                            let_int("body", "1"));
    run_one(interp, std::move(stmt));

    REQUIRE(!is_bound(interp, "body"));
}

TEST_CASE("a while loop iterates until its condition goes false", "[interp]") {
    Interpreter interp;
    // start i at zero, then count up while i < 3. the body re-binds i to i + 1 each
    // pass; since there's no block scope yet the let just overwrites the outer i,
    // which is what makes the condition eventually fail and the loop stop.
    auto seed = let_int("i", "0");

    auto cond = binary(ident("i"), TokenKind::Less, "<", int_lit("3"));
    auto step = std::make_unique<LetStmt>(Token(TokenKind::Identifier, "i", 1, 1),
                                          binary(ident("i"), TokenKind::Plus, "+", int_lit("1")));
    auto loop = std::make_unique<WhileStmt>(std::move(cond), std::move(step));

    std::vector<std::unique_ptr<Stmt>> program;
    program.push_back(std::move(seed));
    program.push_back(std::move(loop));
    interp.interpret(program);

    IdentifierExpr i(Token(TokenKind::Identifier, "i", 1, 1));
    REQUIRE(std::get<int64_t>(interp.evaluate(i)) == 3);
}
