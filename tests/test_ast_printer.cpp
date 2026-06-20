#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "brewc/ast.h"
#include "brewc/ast_printer.h"
#include "brewc/lexer.h"
#include "brewc/parser.h"
#include "brewc/token.h"

using namespace brewc;

namespace {

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

// lex + parse + print an expression so each test is a single string compare.
std::string print_expr(const std::string& source) {
    Parser parser(lex_all(source));
    auto expr = parser.parse_expression();
    AstPrinter printer;
    return printer.print(*expr);
}

std::string print_stmt(const std::string& source) {
    Parser parser(lex_all(source));
    auto stmt = parser.parse_statement();
    AstPrinter printer;
    return printer.print(*stmt);
}

} // namespace

TEST_CASE("printer: a bare literal is just its lexeme", "[printer]") {
    REQUIRE(print_expr("42") == "42");
    REQUIRE(print_expr("3.14") == "3.14");
    REQUIRE(print_expr("\"hi\"") == "hi");
}

TEST_CASE("printer: an identifier prints its name", "[printer]") {
    REQUIRE(print_expr("count") == "count");
}

TEST_CASE("printer: a binary expression wraps the operator first", "[printer]") {
    REQUIRE(print_expr("1 + 2") == "(+ 1 2)");
}

TEST_CASE("printer: precedence shows up in the nesting", "[printer]") {
    REQUIRE(print_expr("1 + 2 * 3 - 4") == "(- (+ 1 (* 2 3)) 4)");
}

TEST_CASE("printer: a unary operator takes a single child", "[printer]") {
    REQUIRE(print_expr("-x") == "(- x)");
    REQUIRE(print_expr("!done") == "(! done)");
}

TEST_CASE("printer: a call lists the callee then its args", "[printer]") {
    REQUIRE(print_expr("add(a, b + 1)") == "(call add a (+ b 1))");
}

TEST_CASE("printer: a no-arg call has nothing after the callee", "[printer]") {
    REQUIRE(print_expr("now()") == "(call now)");
}

TEST_CASE("printer: a let binding prints its name and initializer", "[printer]") {
    REQUIRE(print_stmt("let total = (a + b) * 2") == "(let total (* (+ a b) 2))");
}

TEST_CASE("printer: an if with no else stops after the then branch", "[printer]") {
    REQUIRE(print_stmt("if a { let x = 1 }") == "(if a (block (let x 1)))");
}

TEST_CASE("printer: an if/else carries both branches", "[printer]") {
    REQUIRE(print_stmt("if x < 10 { let y = 1 } else { let y = 2 }") ==
            "(if (< x 10) (block (let y 1)) (block (let y 2)))");
}

TEST_CASE("printer: an else-if chain stays nested", "[printer]") {
    REQUIRE(print_stmt("if a { let x = 1 } else if b { let y = 2 } else { let z = 3 }") ==
            "(if a (block (let x 1)) (if b (block (let y 2)) (block (let z 3))))");
}

TEST_CASE("printer: a while loop prints its condition and body", "[printer]") {
    REQUIRE(print_stmt("while i < n { let i = i + 1 }") ==
            "(while (< i n) (block (let i (+ i 1))))");
}

TEST_CASE("printer: a function shows its params in their own list", "[printer]") {
    REQUIRE(print_stmt("fn add(a, b) { let s = a + b }") ==
            "(fn add (a b) (block (let s (+ a b))))");
}

TEST_CASE("printer: a no-arg function has an empty param list", "[printer]") {
    REQUIRE(print_stmt("fn main() {}") == "(fn main () (block))");
}

TEST_CASE("printer: an empty block prints with no children", "[printer]") {
    REQUIRE(print_stmt("while running {}") == "(while running (block))");
}

TEST_CASE("printer: the same instance can be reused across nodes", "[printer]") {
    Parser parser(lex_all("let x = 1 let y = x + 2"));
    auto program = parser.parse_program();
    REQUIRE(parser.errors().empty());
    REQUIRE(program.size() == 2);

    AstPrinter printer;
    REQUIRE(printer.print(*program[0]) == "(let x 1)");
    REQUIRE(printer.print(*program[1]) == "(let y (+ x 2))");
}
