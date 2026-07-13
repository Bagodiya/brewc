#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "brewc/interpreter.h"
#include "brewc/lexer.h"
#include "brewc/parser.h"
#include "brewc/runtime_error.h"
#include "brewc/token.h"

using namespace brewc;

namespace {

// same lex-everything-then-parse pair the other suites use, so these tests feed
// the interpreter the exact token stream the real front end would.
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

// run a program that's expected to blow up and hand the RuntimeError back so a
// test can poke at its line, column and trace. if the program somehow finishes
// clean we throw a plain runtime_error instead, which trips the REQUIRE in the
// caller rather than silently passing.
RuntimeError run_and_catch(const std::string& source) {
    Interpreter interp;
    auto program = parse_program(source);
    try {
        interp.interpret(program);
    } catch (const RuntimeError& e) {
        return e;
    }
    throw std::runtime_error("program was expected to fail but ran clean");
}

// a two-function program that divides by zero at the very bottom of the call
// chain. laid out so the line numbers are easy to eyeball:
//   1: fn inner(n) {
//   2:   let x = 1 / 0
//   3: }
//   4: fn outer(n) {
//   5:   let y = inner(n)
//   6: }
//   7: let go = outer(5)
const char* kNestedBoom =
    "fn inner(n) {\n"
    "  let x = 1 / 0\n"
    "}\n"
    "fn outer(n) {\n"
    "  let y = inner(n)\n"
    "}\n"
    "let go = outer(5)\n";

} // namespace

TEST_CASE("a RuntimeError is still a std::runtime_error", "[runtime]") {
    // older catch sites only know about the base type, so this has to keep holding.
    RuntimeError err("boom", 1, 1, {});
    const std::runtime_error& base = err;
    REQUIRE(std::string(base.what()) == "boom");
}

TEST_CASE("a divide by zero reports the line it happened on", "[runtime]") {
    RuntimeError err = run_and_catch("let a = 1\n"
                                     "let bad = 5 / 0\n");
    REQUIRE(std::string(err.what()) == "division by zero");
    REQUIRE(err.line() == 2);
}

TEST_CASE("an undefined variable reports its own location", "[runtime]") {
    RuntimeError err = run_and_catch("let x = missing\n");
    REQUIRE(std::string(err.what()).find("undefined variable") != std::string::npos);
    REQUIRE(err.line() == 1);
}

TEST_CASE("an error at the top level carries no stack trace", "[runtime]") {
    RuntimeError err = run_and_catch("let bad = 5 / 0\n");
    REQUIRE(err.trace().empty());
}

TEST_CASE("an error inside nested calls records the whole chain", "[runtime]") {
    RuntimeError err = run_and_catch(kNestedBoom);

    // the divide lives on line 2, inside inner.
    REQUIRE(std::string(err.what()) == "division by zero");
    REQUIRE(err.line() == 2);

    // two calls were open: outer (called from the top level on line 7) and inner
    // (called from inside outer on line 5). oldest is at the front.
    REQUIRE(err.trace().size() == 2);
    REQUIRE(err.trace()[0].fn_name == "outer");
    REQUIRE(err.trace()[0].call_line == 7);
    REQUIRE(err.trace()[1].fn_name == "inner");
    REQUIRE(err.trace()[1].call_line == 5);
}

TEST_CASE("a call scope is popped off the stack once it returns cleanly", "[runtime]") {
    // outer runs and returns fine, then a separate top-level divide fails. if the
    // frames weren't being popped, outer would still be hanging around in the trace.
    RuntimeError err = run_and_catch("fn outer() {\n"
                                     "  let ok = 1\n"
                                     "}\n"
                                     "let done = outer()\n"
                                     "let bad = 9 / 0\n");
    REQUIRE(err.trace().empty());
}

TEST_CASE("format_error prints the message, the location and the stack", "[runtime]") {
    RuntimeError err = run_and_catch(kNestedBoom);
    std::string report = format_error(err);

    REQUIRE(report.find("runtime error: division by zero") != std::string::npos);
    REQUIRE(report.find("line 2") != std::string::npos);
    REQUIRE(report.find("stack trace:") != std::string::npos);
    // innermost call comes out on top, the way a backtrace usually reads.
    REQUIRE(report.find("in inner() called from line 5") != std::string::npos);
    REQUIRE(report.find("in outer() called from line 7") != std::string::npos);
    REQUIRE(report.find("in inner") < report.find("in outer"));
}

TEST_CASE("format_error leaves out the stack section for a top-level error", "[runtime]") {
    RuntimeError err = run_and_catch("let bad = 5 / 0\n");
    std::string report = format_error(err);

    REQUIRE(report.find("runtime error: division by zero") != std::string::npos);
    REQUIRE(report.find("stack trace:") == std::string::npos);
}
