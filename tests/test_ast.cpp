#include <catch2/catch_test_macros.hpp>

#include <string>

#include "brewc/ast.h"

using namespace brewc;

namespace {

// tiny visitor that just records which node it landed on plus a bit of the
// node's data, so we can check accept() routes to the right place.
struct RecordingVisitor : Visitor {
    std::string last;

    void visit_literal(LiteralExpr& expr) override {
        last = "literal:" + expr.token.lexeme;
    }

    void visit_identifier(IdentifierExpr& expr) override {
        last = "identifier:" + expr.name;
    }
};

} // namespace

TEST_CASE("literal expr keeps its token", "[ast]") {
    LiteralExpr lit(Token(TokenKind::Integer, "42", 1, 1));
    REQUIRE(lit.token.kind == TokenKind::Integer);
    REQUIRE(lit.token.lexeme == "42");
}

TEST_CASE("identifier expr exposes its name", "[ast]") {
    IdentifierExpr id(Token(TokenKind::Identifier, "count", 2, 5));
    REQUIRE(id.name == "count");
    REQUIRE(id.token.line == 2);
    REQUIRE(id.token.column == 5);
}

TEST_CASE("accept dispatches literals to the visitor", "[ast]") {
    RecordingVisitor visitor;
    LiteralExpr lit(Token(TokenKind::Float, "3.14", 1, 1));
    lit.accept(visitor);
    REQUIRE(visitor.last == "literal:3.14");
}

TEST_CASE("accept dispatches identifiers to the visitor", "[ast]") {
    RecordingVisitor visitor;
    IdentifierExpr id(Token(TokenKind::Identifier, "x", 1, 1));
    id.accept(visitor);
    REQUIRE(visitor.last == "identifier:x");
}

TEST_CASE("visitor works through an Expr base pointer", "[ast]") {
    RecordingVisitor visitor;
    Expr* node = new IdentifierExpr(Token(TokenKind::Identifier, "total", 1, 1));
    node->accept(visitor);
    REQUIRE(visitor.last == "identifier:total");
    delete node;
}
