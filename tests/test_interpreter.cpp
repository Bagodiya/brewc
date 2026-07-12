#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
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

// bundle a list of statements into a block node. tests build the vector up first
// and hand it over, which keeps the block wiring out of each case.
std::unique_ptr<BlockStmt> block(std::vector<std::unique_ptr<Stmt>> stmts) {
    return std::make_unique<BlockStmt>(std::move(stmts));
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

TEST_CASE("an empty block does nothing", "[interp]") {
    Interpreter interp;
    std::vector<std::unique_ptr<Stmt>> body;
    REQUIRE_NOTHROW(run_one(interp, block(std::move(body))));
}

TEST_CASE("a let inside a block does not leak to the outer scope", "[interp]") {
    Interpreter interp;
    std::vector<std::unique_ptr<Stmt>> body;
    body.push_back(let_int("inner", "1"));
    run_one(interp, block(std::move(body)));

    // once the block's scope is gone, its bindings should be gone with it.
    REQUIRE(!is_bound(interp, "inner"));
}

TEST_CASE("a block can read a binding from the enclosing scope", "[interp]") {
    Interpreter interp;
    // reading the outer x from inside the block has to work; if the block couldn't
    // see out, the x + 1 read would throw undefined variable and this would fail.
    std::vector<std::unique_ptr<Stmt>> body;
    body.push_back(std::make_unique<LetStmt>(Token(TokenKind::Identifier, "y", 1, 1),
                                             binary(ident("x"), TokenKind::Plus, "+", int_lit("1"))));

    std::vector<std::unique_ptr<Stmt>> program;
    program.push_back(let_int("x", "7"));
    program.push_back(block(std::move(body)));
    REQUIRE_NOTHROW(interp.interpret(program));
}

TEST_CASE("a let in a block shadows an outer binding without touching it", "[interp]") {
    Interpreter interp;
    std::vector<std::unique_ptr<Stmt>> body;
    body.push_back(let_int("n", "99")); // this n only exists inside the block

    std::vector<std::unique_ptr<Stmt>> program;
    program.push_back(let_int("n", "1"));
    program.push_back(block(std::move(body)));
    interp.interpret(program);

    // the block bound its own n, so the outer one should still read back as 1.
    IdentifierExpr n(Token(TokenKind::Identifier, "n", 1, 1));
    REQUIRE(std::get<int64_t>(interp.evaluate(n)) == 1);
}

TEST_CASE("a function declaration binds a callable value", "[interp]") {
    Interpreter interp;
    auto program = parse_program("fn add(a, b) { }");
    interp.interpret(program);

    // running the decl should leave `add` bound to a function value, the same way
    // a let would leave a name bound to a number.
    IdentifierExpr add(Token(TokenKind::Identifier, "add", 1, 1));
    Value v = interp.evaluate(add);
    REQUIRE(is_function(v));
}

TEST_CASE("a function value points back at its declaration", "[interp]") {
    Interpreter interp;
    auto program = parse_program("fn greet() { }");
    interp.interpret(program);

    IdentifierExpr greet(Token(TokenKind::Identifier, "greet", 1, 1));
    Value v = interp.evaluate(greet);
    REQUIRE(is_function(v));

    // the value carries the decl so a later call step can find the body and params.
    Function fn = std::get<Function>(v);
    REQUIRE(fn.decl != nullptr);
    REQUIRE(fn.decl->name == "greet");
}

TEST_CASE("a function bound at top level does not leak out of a block", "[interp]") {
    Interpreter interp;
    // a fn declared inside braces should behave like any other block binding: gone
    // once the block finishes, since visit_fn just defines into the current scope.
    std::vector<std::unique_ptr<Stmt>> body;
    body.push_back(std::make_unique<FnDecl>(Token(TokenKind::Identifier, "hidden", 1, 1),
                                            std::vector<Token>{},
                                            block({})));
    run_one(interp, block(std::move(body)));

    REQUIRE(!is_bound(interp, "hidden"));
}

// build a call node by hand so a test can invoke a function that's already been
// defined without needing an expression-statement rule in the parser yet.
std::unique_ptr<CallExpr> call(std::unique_ptr<Expr> callee,
                               std::vector<std::unique_ptr<Expr>> args) {
    return std::make_unique<CallExpr>(std::move(callee), Token(TokenKind::LParen, "(", 1, 1),
                                      std::move(args));
}

// shorthand for the common "call by name" case: look up a function under `name`
// and pass it the given arguments.
std::unique_ptr<CallExpr> call_named(const std::string& name,
                                     std::vector<std::unique_ptr<Expr>> args) {
    return call(ident(name), std::move(args));
}

// hand-build `fn name(params) { body }`. the call tests reach for this instead of
// the parser so they only exercise the interpreter and don't ride on however the
// parser happens to handle a body full of statements.
std::unique_ptr<FnDecl> fn_decl(const std::string& name, std::vector<std::string> params,
                                std::vector<std::unique_ptr<Stmt>> body_stmts) {
    std::vector<Token> param_tokens;
    for (const auto& p : params) {
        param_tokens.push_back(Token(TokenKind::Identifier, p, 1, 1));
    }
    return std::make_unique<FnDecl>(Token(TokenKind::Identifier, name, 1, 1),
                                    std::move(param_tokens), block(std::move(body_stmts)));
}

TEST_CASE("calling a no-arg function runs its body", "[interp]") {
    Interpreter interp;
    auto program = parse_program("fn nothing() { }");
    interp.interpret(program);

    auto c = call_named("nothing", {});
    REQUIRE_NOTHROW(interp.evaluate(*c));
}

TEST_CASE("a call has no return value yet so it comes back as nil", "[interp]") {
    Interpreter interp;
    auto program = parse_program("fn nothing() { }");
    interp.interpret(program);

    auto c = call_named("nothing", {});
    Value v = interp.evaluate(*c);
    REQUIRE(is_nil(v));
}

TEST_CASE("an argument value is bound to the matching parameter", "[interp]") {
    Interpreter interp;
    // fn f(x) { let y = 10 / x; } — dividing ten by the parameter, so passing zero
    // surfaces as a division-by-zero error. that only fires if x really got bound to
    // 0, which is what proves the argument reached the parameter. the program vector
    // is kept alive for the whole test so the function value's decl stays valid.
    std::vector<std::unique_ptr<Stmt>> body;
    body.push_back(std::make_unique<LetStmt>(
        Token(TokenKind::Identifier, "y", 1, 1),
        binary(int_lit("10"), TokenKind::Slash, "/", ident("x"))));

    std::vector<std::unique_ptr<Stmt>> program;
    program.push_back(fn_decl("f", {"x"}, std::move(body)));
    interp.interpret(program);

    std::vector<std::unique_ptr<Expr>> zero_arg;
    zero_arg.push_back(int_lit("0"));
    auto boom = call_named("f", std::move(zero_arg));
    REQUIRE_THROWS_AS(interp.evaluate(*boom), std::runtime_error);

    std::vector<std::unique_ptr<Expr>> good_arg;
    good_arg.push_back(int_lit("2"));
    auto ok = call_named("f", std::move(good_arg));
    REQUIRE_NOTHROW(interp.evaluate(*ok));
}

TEST_CASE("each parameter lines up with its own argument", "[interp]") {
    Interpreter interp;
    // fn g(a, b) { let z = 1 / (a - b); } — a minus b as a divisor. equal arguments
    // make it zero and blow up; a mismatched pair is fine. if the two arguments bound
    // in the wrong order this wouldn't tell us anything, but since a - b isn't
    // symmetric the ordering is exactly what's under test.
    std::vector<std::unique_ptr<Stmt>> body;
    body.push_back(std::make_unique<LetStmt>(
        Token(TokenKind::Identifier, "z", 1, 1),
        binary(int_lit("1"), TokenKind::Slash, "/",
               binary(ident("a"), TokenKind::Minus, "-", ident("b")))));

    std::vector<std::unique_ptr<Stmt>> program;
    program.push_back(fn_decl("g", {"a", "b"}, std::move(body)));
    interp.interpret(program);

    std::vector<std::unique_ptr<Expr>> equal;
    equal.push_back(int_lit("5"));
    equal.push_back(int_lit("5"));
    auto same = call_named("g", std::move(equal));
    REQUIRE_THROWS_AS(interp.evaluate(*same), std::runtime_error);

    std::vector<std::unique_ptr<Expr>> different;
    different.push_back(int_lit("5"));
    different.push_back(int_lit("3"));
    auto diff = call_named("g", std::move(different));
    REQUIRE_NOTHROW(interp.evaluate(*diff));
}

TEST_CASE("a parameter does not leak into the outer scope after the call", "[interp]") {
    Interpreter interp;
    auto program = parse_program("fn f(secret) { }");
    interp.interpret(program);

    std::vector<std::unique_ptr<Expr>> arg;
    arg.push_back(int_lit("1"));
    auto c = call_named("f", std::move(arg));
    interp.evaluate(*c);

    // `secret` only exists for the length of the call, so it must be gone now.
    REQUIRE(!is_bound(interp, "secret"));
}

TEST_CASE("calling with the wrong number of arguments is an error", "[interp]") {
    Interpreter interp;
    auto program = parse_program("fn takes_one(a) { }");
    interp.interpret(program);

    auto too_few = call_named("takes_one", {});
    REQUIRE_THROWS_AS(interp.evaluate(*too_few), std::runtime_error);

    std::vector<std::unique_ptr<Expr>> two;
    two.push_back(int_lit("1"));
    two.push_back(int_lit("2"));
    auto too_many = call_named("takes_one", std::move(two));
    REQUIRE_THROWS_AS(interp.evaluate(*too_many), std::runtime_error);
}

TEST_CASE("calling something that isn't a function is an error", "[interp]") {
    Interpreter interp;
    // the callee is a plain integer literal, which has no body to run.
    std::vector<std::unique_ptr<Expr>> args;
    auto c = call(int_lit("42"), std::move(args));
    REQUIRE_THROWS_AS(interp.evaluate(*c), std::runtime_error);
}

// run a program and hand back whatever error message it threw, or the empty
// string if it finished cleanly. the recursion tests use this to tell apart a
// base case that was actually reached from a name that never resolved.
std::string run_error(Interpreter& interp, std::vector<std::unique_ptr<Stmt>>& program) {
    try {
        interp.interpret(program);
        return "";
    } catch (const std::runtime_error& e) {
        return e.what();
    }
}

TEST_CASE("a top-level function can call itself", "[interp]") {
    Interpreter interp;
    // down just counts to zero. it never errors, so the only way this throws is if
    // the recursive call couldn't find `down` from inside the body.
    auto program = parse_program("fn down(n) {"
                                 "  if n > 0 {"
                                 "    let step = down(n - 1)"
                                 "  }"
                                 "}"
                                 "let go = down(5)");
    REQUIRE_NOTHROW(interp.interpret(program));
}

TEST_CASE("recursion runs all the way down to the base case", "[interp]") {
    Interpreter interp;
    // count down from 3 and divide by zero once we hit the bottom. getting the
    // division error means every level fired and the base case was reached; if the
    // body couldn't see its own name we'd get an 'undefined variable' error long
    // before that, so the message is what proves the recursion really happened.
    auto program = parse_program("fn count(n) {"
                                 "  if n == 0 {"
                                 "    let boom = 1 / 0"
                                 "  } else {"
                                 "    let step = count(n - 1)"
                                 "  }"
                                 "}"
                                 "let go = count(3)");
    std::string err = run_error(interp, program);
    REQUIRE(err.find("division by zero") != std::string::npos);
}

TEST_CASE("a function declared inside a block can still recurse", "[interp]") {
    Interpreter interp;
    // count lives only inside a block, so it never reaches the globals. the call
    // scope has to hang off the block scope where count was declared, otherwise the
    // recursive call goes looking in the globals and comes up empty. build it by
    // hand since a bare block isn't a top-level statement the parser accepts.

    // inner recursive call: count(n - 1)
    std::vector<std::unique_ptr<Expr>> rec_args;
    rec_args.push_back(binary(ident("n"), TokenKind::Minus, "-", int_lit("1")));
    auto rec_let = std::make_unique<LetStmt>(Token(TokenKind::Identifier, "step", 1, 1),
                                             call_named("count", std::move(rec_args)));

    // then-branch: { let step = count(n - 1); }
    std::vector<std::unique_ptr<Stmt>> then_stmts;
    then_stmts.push_back(std::move(rec_let));

    // if n > 0 { ... }
    auto cond = binary(ident("n"), TokenKind::Greater, ">", int_lit("0"));
    auto guard = std::make_unique<IfStmt>(std::move(cond), block(std::move(then_stmts)), nullptr);

    std::vector<std::unique_ptr<Stmt>> fn_body;
    fn_body.push_back(std::move(guard));

    // let go = count(3);
    std::vector<std::unique_ptr<Expr>> start_args;
    start_args.push_back(int_lit("3"));
    auto start = std::make_unique<LetStmt>(Token(TokenKind::Identifier, "go", 1, 1),
                                           call_named("count", std::move(start_args)));

    // { fn count(n) { ... } let go = count(3); }
    std::vector<std::unique_ptr<Stmt>> outer;
    outer.push_back(fn_decl("count", {"n"}, std::move(fn_body)));
    outer.push_back(std::move(start));

    REQUIRE_NOTHROW(run_one(interp, block(std::move(outer))));
}

TEST_CASE("a function closes over a variable from the scope it was declared in", "[interp]") {
    Interpreter interp;
    // reader captures `base` from the enclosing block, not from its own params and
    // not from the globals. calling it divides 1 / base, and since base is 0 the
    // only way this reaches a division-by-zero is if the closure actually saw the
    // captured binding. if closures didn't reach the enclosing scope we'd trip over
    // 'undefined variable base' first, so the error message is what proves capture.
    std::vector<std::unique_ptr<Stmt>> reader_body;
    reader_body.push_back(std::make_unique<LetStmt>(
        Token(TokenKind::Identifier, "boom", 1, 1),
        binary(int_lit("1"), TokenKind::Slash, "/", ident("base"))));

    std::vector<std::unique_ptr<Expr>> no_args;
    auto invoke = std::make_unique<LetStmt>(Token(TokenKind::Identifier, "go", 1, 1),
                                            call_named("reader", std::move(no_args)));

    std::vector<std::unique_ptr<Stmt>> outer;
    outer.push_back(let_int("base", "0"));
    outer.push_back(fn_decl("reader", {}, std::move(reader_body)));
    outer.push_back(std::move(invoke));

    std::vector<std::unique_ptr<Stmt>> program;
    program.push_back(block(std::move(outer)));
    std::string err = run_error(interp, program);
    REQUIRE(err.find("division by zero") != std::string::npos);
}

// swap std::cout's buffer out for a stringstream while a call runs, then hand back
// everything print wrote. restoring the old buffer in every path keeps a later
// test from losing its output if something here throws.
std::string capture_output(Interpreter& interp, CallExpr& call_expr) {
    std::ostringstream sink;
    std::streambuf* previous = std::cout.rdbuf(sink.rdbuf());
    try {
        interp.evaluate(call_expr);
    } catch (...) {
        std::cout.rdbuf(previous);
        throw;
    }
    std::cout.rdbuf(previous);
    return sink.str();
}

TEST_CASE("print is available as a builtin in the global scope", "[interp]") {
    Interpreter interp;
    IdentifierExpr name(Token(TokenKind::Identifier, "print", 1, 1));
    Value v = interp.evaluate(name);
    REQUIRE(is_native(v));
}

TEST_CASE("calling print returns nil", "[interp]") {
    Interpreter interp;
    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(int_lit("1"));
    auto c = call_named("print", std::move(args));

    // still redirect so the test run doesn't spew the number onto the terminal.
    std::ostringstream sink;
    std::streambuf* previous = std::cout.rdbuf(sink.rdbuf());
    Value v = interp.evaluate(*c);
    std::cout.rdbuf(previous);

    REQUIRE(is_nil(v));
}

TEST_CASE("print writes an integer followed by a newline", "[interp]") {
    Interpreter interp;
    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(int_lit("42"));
    auto c = call_named("print", std::move(args));
    REQUIRE(capture_output(interp, *c) == "42\n");
}

TEST_CASE("print renders each value variant the way the language shows it", "[interp]") {
    Interpreter interp;

    std::vector<std::unique_ptr<Expr>> num;
    num.push_back(literal(TokenKind::Float, "3.0"));
    auto flt = call_named("print", std::move(num));
    REQUIRE(capture_output(interp, *flt) == "3.0\n");

    std::vector<std::unique_ptr<Expr>> yes;
    yes.push_back(literal(TokenKind::True, "true"));
    auto b = call_named("print", std::move(yes));
    REQUIRE(capture_output(interp, *b) == "true\n");

    std::vector<std::unique_ptr<Expr>> text;
    text.push_back(literal(TokenKind::String, "hello"));
    auto s = call_named("print", std::move(text));
    REQUIRE(capture_output(interp, *s) == "hello\n");

    std::vector<std::unique_ptr<Expr>> nothing;
    nothing.push_back(literal(TokenKind::Nil, "nil"));
    auto n = call_named("print", std::move(nothing));
    REQUIRE(capture_output(interp, *n) == "nil\n");
}

TEST_CASE("print separates several arguments with a single space", "[interp]") {
    Interpreter interp;
    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(int_lit("1"));
    args.push_back(int_lit("2"));
    args.push_back(int_lit("3"));
    auto c = call_named("print", std::move(args));
    REQUIRE(capture_output(interp, *c) == "1 2 3\n");
}

TEST_CASE("print with no arguments just writes a blank line", "[interp]") {
    Interpreter interp;
    auto c = call_named("print", {});
    REQUIRE(capture_output(interp, *c) == "\n");
}

TEST_CASE("a function value prints with its name", "[interp]") {
    Interpreter interp;
    auto program = parse_program("fn add(a, b) { }");
    interp.interpret(program);

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(ident("add"));
    auto c = call_named("print", std::move(args));
    REQUIRE(capture_output(interp, *c) == "<fn add>\n");
}

TEST_CASE("a closure sees the enclosing value at the moment it is called", "[interp]") {
    Interpreter interp;
    // watcher reads `n` from the enclosing scope. we bump n from 1 to 0 before the
    // call, then invoke watcher — dividing 1 / n blows up only because the closure
    // reads the live enclosing binding rather than a stale copy taken at declare
    // time. a snapshot-at-declaration would still see n == 1 and finish cleanly.
    std::vector<std::unique_ptr<Stmt>> watcher_body;
    watcher_body.push_back(std::make_unique<LetStmt>(
        Token(TokenKind::Identifier, "hit", 1, 1),
        binary(int_lit("1"), TokenKind::Slash, "/", ident("n"))));

    std::vector<std::unique_ptr<Expr>> no_args;
    auto invoke = std::make_unique<LetStmt>(Token(TokenKind::Identifier, "go", 1, 1),
                                            call_named("watcher", std::move(no_args)));

    std::vector<std::unique_ptr<Stmt>> outer;
    outer.push_back(let_int("n", "1"));
    outer.push_back(fn_decl("watcher", {}, std::move(watcher_body)));
    outer.push_back(let_int("n", "0")); // redefine n in the same scope before the call
    outer.push_back(std::move(invoke));

    std::vector<std::unique_ptr<Stmt>> program;
    program.push_back(block(std::move(outer)));
    std::string err = run_error(interp, program);
    REQUIRE(err.find("division by zero") != std::string::npos);
}

TEST_CASE("clock returns a float number of seconds", "[interp]") {
    Interpreter interp;
    auto c = call_named("clock", {});
    Value v = interp.evaluate(*c);
    REQUIRE(is_float(v));
}

TEST_CASE("clock does not go backwards between two calls", "[interp]") {
    Interpreter interp;
    // two readings back to back. steady_clock never runs backwards, so the second
    // reading has to be at least as large as the first. can't check for a strict
    // increase since the two calls might land inside the same clock tick.
    auto first = call_named("clock", {});
    double before = std::get<double>(interp.evaluate(*first));
    auto second = call_named("clock", {});
    double after = std::get<double>(interp.evaluate(*second));
    REQUIRE(after >= before);
}
