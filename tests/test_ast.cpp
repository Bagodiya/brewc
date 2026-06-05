#include <catch2/catch_test_macros.hpp>

#include <memory>
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

    void visit_binary(BinaryExpr& expr) override {
        last = "binary:" + expr.op.lexeme;
    }

    void visit_unary(UnaryExpr& expr) override {
        last = "unary:" + expr.op.lexeme;
    }
};

// same idea for statements, just over the StmtVisitor side of the tree.
struct RecordingStmtVisitor : StmtVisitor {
    std::string last;

    void visit_let(LetStmt& stmt) override {
        last = "let:" + stmt.name;
    }

    void visit_if(IfStmt& stmt) override {
        last = stmt.else_branch ? "if:else" : "if:noelse";
    }

    void visit_while(WhileStmt&) override {
        last = "while";
    }

    void visit_block(BlockStmt& stmt) override {
        last = "block:" + std::to_string(stmt.statements.size());
    }
};

// builds a throwaway `let x = 1` so the control-flow tests have a body to hold.
std::unique_ptr<Stmt> dummy_let() {
    return std::make_unique<LetStmt>(Token(TokenKind::Identifier, "x", 1, 1),
                                     std::make_unique<LiteralExpr>(
                                         Token(TokenKind::Integer, "1", 1, 1)));
}

// small helper so the tests don't drown in Token boilerplate.
std::unique_ptr<Expr> int_lit(const std::string& text) {
    return std::make_unique<LiteralExpr>(Token(TokenKind::Integer, text, 1, 1));
}

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

TEST_CASE("binary expr holds both operands and the operator", "[ast]") {
    BinaryExpr add(int_lit("1"), Token(TokenKind::Plus, "+", 1, 3), int_lit("2"));
    REQUIRE(add.op.kind == TokenKind::Plus);
    auto* lhs = dynamic_cast<LiteralExpr*>(add.left.get());
    auto* rhs = dynamic_cast<LiteralExpr*>(add.right.get());
    REQUIRE(lhs != nullptr);
    REQUIRE(rhs != nullptr);
    REQUIRE(lhs->token.lexeme == "1");
    REQUIRE(rhs->token.lexeme == "2");
}

TEST_CASE("unary expr holds one operand and the operator", "[ast]") {
    UnaryExpr neg(Token(TokenKind::Minus, "-", 1, 1), int_lit("5"));
    REQUIRE(neg.op.kind == TokenKind::Minus);
    auto* inner = dynamic_cast<LiteralExpr*>(neg.operand.get());
    REQUIRE(inner != nullptr);
    REQUIRE(inner->token.lexeme == "5");
}

TEST_CASE("accept dispatches binary nodes to the visitor", "[ast]") {
    RecordingVisitor visitor;
    BinaryExpr cmp(int_lit("3"), Token(TokenKind::Less, "<", 1, 3), int_lit("4"));
    cmp.accept(visitor);
    REQUIRE(visitor.last == "binary:<");
}

TEST_CASE("accept dispatches unary nodes to the visitor", "[ast]") {
    RecordingVisitor visitor;
    UnaryExpr bang(Token(TokenKind::Bang, "!", 1, 1), int_lit("0"));
    bang.accept(visitor);
    REQUIRE(visitor.last == "unary:!");
}

TEST_CASE("let stmt keeps its name and initializer", "[ast]") {
    // let total = 42
    LetStmt stmt(Token(TokenKind::Identifier, "total", 3, 5), int_lit("42"));
    REQUIRE(stmt.name == "total");
    REQUIRE(stmt.name_token.line == 3);
    REQUIRE(stmt.name_token.column == 5);
    auto* init = dynamic_cast<LiteralExpr*>(stmt.initializer.get());
    REQUIRE(init != nullptr);
    REQUIRE(init->token.lexeme == "42");
}

TEST_CASE("accept dispatches let stmts to the visitor", "[ast]") {
    RecordingStmtVisitor visitor;
    LetStmt stmt(Token(TokenKind::Identifier, "x", 1, 5), int_lit("1"));
    stmt.accept(visitor);
    REQUIRE(visitor.last == "let:x");
}

TEST_CASE("let stmt works through a Stmt base pointer", "[ast]") {
    RecordingStmtVisitor visitor;
    std::unique_ptr<Stmt> node =
        std::make_unique<LetStmt>(Token(TokenKind::Identifier, "count", 1, 5), int_lit("0"));
    node->accept(visitor);
    REQUIRE(visitor.last == "let:count");
}

TEST_CASE("block stmt owns its statements in order", "[ast]") {
    std::vector<std::unique_ptr<Stmt>> body;
    body.push_back(dummy_let());
    body.push_back(dummy_let());
    BlockStmt block(std::move(body));
    REQUIRE(block.statements.size() == 2);
    REQUIRE(dynamic_cast<LetStmt*>(block.statements[0].get()) != nullptr);
}

TEST_CASE("if stmt without else leaves else_branch null", "[ast]") {
    IfStmt stmt(int_lit("1"), dummy_let(), nullptr);
    REQUIRE(stmt.then_branch != nullptr);
    REQUIRE(stmt.else_branch == nullptr);
}

TEST_CASE("if stmt with else holds both branches", "[ast]") {
    IfStmt stmt(int_lit("1"), dummy_let(), dummy_let());
    REQUIRE(stmt.then_branch != nullptr);
    REQUIRE(stmt.else_branch != nullptr);
}

TEST_CASE("while stmt keeps condition and body", "[ast]") {
    WhileStmt stmt(int_lit("1"), dummy_let());
    REQUIRE(stmt.condition != nullptr);
    REQUIRE(stmt.body != nullptr);
}

TEST_CASE("accept routes control-flow stmts to the right visit", "[ast]") {
    RecordingStmtVisitor visitor;

    IfStmt no_else(int_lit("1"), dummy_let(), nullptr);
    no_else.accept(visitor);
    REQUIRE(visitor.last == "if:noelse");

    IfStmt with_else(int_lit("1"), dummy_let(), dummy_let());
    with_else.accept(visitor);
    REQUIRE(visitor.last == "if:else");

    WhileStmt loop(int_lit("1"), dummy_let());
    loop.accept(visitor);
    REQUIRE(visitor.last == "while");

    std::vector<std::unique_ptr<Stmt>> body;
    body.push_back(dummy_let());
    BlockStmt block(std::move(body));
    block.accept(visitor);
    REQUIRE(visitor.last == "block:1");
}

TEST_CASE("nested binary expr keeps its subtree alive", "[ast]") {
    // (1 + 2) * 3 — make sure ownership nests without leaking or dangling.
    auto inner = std::make_unique<BinaryExpr>(int_lit("1"),
                                              Token(TokenKind::Plus, "+", 1, 3),
                                              int_lit("2"));
    BinaryExpr outer(std::move(inner), Token(TokenKind::Star, "*", 1, 7), int_lit("3"));

    auto* left = dynamic_cast<BinaryExpr*>(outer.left.get());
    REQUIRE(left != nullptr);
    REQUIRE(left->op.kind == TokenKind::Plus);
    REQUIRE(outer.op.kind == TokenKind::Star);
}
