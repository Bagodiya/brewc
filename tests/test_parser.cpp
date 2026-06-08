#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "brewc/ast.h"
#include "brewc/lexer.h"
#include "brewc/parser.h"
#include "brewc/token.h"

using namespace brewc;

namespace {

// run the source through the lexer and collect every token up to and including
// End, so we can hand the parser the same flat list it sees in the real driver.
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

// lex + parse a single expression in one go.
std::unique_ptr<Expr> parse_expr(const std::string& source) {
    Parser parser(lex_all(source));
    return parser.parse_expression();
}

} // namespace

TEST_CASE("parses an integer literal", "[parser]") {
    auto expr = parse_expr("42");
    auto* lit = dynamic_cast<LiteralExpr*>(expr.get());
    REQUIRE(lit != nullptr);
    REQUIRE(lit->token.kind == TokenKind::Integer);
    REQUIRE(lit->token.lexeme == "42");
}

TEST_CASE("parses a float literal", "[parser]") {
    auto expr = parse_expr("3.14");
    auto* lit = dynamic_cast<LiteralExpr*>(expr.get());
    REQUIRE(lit != nullptr);
    REQUIRE(lit->token.kind == TokenKind::Float);
    REQUIRE(lit->token.lexeme == "3.14");
}

TEST_CASE("parses a string literal", "[parser]") {
    auto expr = parse_expr("\"hello\"");
    auto* lit = dynamic_cast<LiteralExpr*>(expr.get());
    REQUIRE(lit != nullptr);
    REQUIRE(lit->token.kind == TokenKind::String);
    REQUIRE(lit->token.lexeme == "hello");
}

TEST_CASE("parses the boolean and nil keywords as literals", "[parser]") {
    auto t = parse_expr("true");
    REQUIRE(dynamic_cast<LiteralExpr*>(t.get()) != nullptr);
    REQUIRE(dynamic_cast<LiteralExpr*>(t.get())->token.kind == TokenKind::True);

    auto f = parse_expr("false");
    REQUIRE(dynamic_cast<LiteralExpr*>(f.get()) != nullptr);
    REQUIRE(dynamic_cast<LiteralExpr*>(f.get())->token.kind == TokenKind::False);

    auto n = parse_expr("nil");
    REQUIRE(dynamic_cast<LiteralExpr*>(n.get()) != nullptr);
    REQUIRE(dynamic_cast<LiteralExpr*>(n.get())->token.kind == TokenKind::Nil);
}

TEST_CASE("parses an identifier", "[parser]") {
    auto expr = parse_expr("count");
    auto* id = dynamic_cast<IdentifierExpr*>(expr.get());
    REQUIRE(id != nullptr);
    REQUIRE(id->name == "count");
}

TEST_CASE("a parenthesised expression unwraps to the inner node", "[parser]") {
    auto expr = parse_expr("(7)");
    auto* lit = dynamic_cast<LiteralExpr*>(expr.get());
    REQUIRE(lit != nullptr);
    REQUIRE(lit->token.lexeme == "7");
}

TEST_CASE("nested parens still unwrap to the innermost node", "[parser]") {
    auto expr = parse_expr("(((x)))");
    auto* id = dynamic_cast<IdentifierExpr*>(expr.get());
    REQUIRE(id != nullptr);
    REQUIRE(id->name == "x");
}

TEST_CASE("a missing closing paren is reported", "[parser]") {
    REQUIRE_THROWS(parse_expr("(1"));
}

TEST_CASE("a token that can't start an expression is reported", "[parser]") {
    REQUIRE_THROWS(parse_expr("+"));
}
