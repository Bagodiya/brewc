#ifndef BREWC_AST_H
#define BREWC_AST_H

#include <memory>
#include <string>
#include <utility>

#include "brewc/token.h"

namespace brewc {

// forward declare the visitor so Expr can mention it before it's defined.
// the actual visit_* methods get added to it as we introduce each node type
// in the next few steps.
class Visitor;

// base class for every expression node. concrete nodes derive from this and
// implement accept(), which just turns around and calls the matching visit_*
// on whatever visitor is passed in (double dispatch).
class Expr {
public:
    virtual ~Expr() = default;
    virtual void accept(Visitor& visitor) = 0;
};

// need these forward declared too, otherwise Visitor can't name them in the
// visit_* signatures below.
class LiteralExpr;
class IdentifierExpr;
class BinaryExpr;
class UnaryExpr;

// anything that wants to walk the tree (the printer, the interpreter, ...)
// inherits from this. one visit_* per concrete node type; we keep them pure
// so every visitor is forced to handle every node.
class Visitor {
public:
    virtual ~Visitor() = default;
    virtual void visit_literal(LiteralExpr& expr) = 0;
    virtual void visit_identifier(IdentifierExpr& expr) = 0;
    virtual void visit_binary(BinaryExpr& expr) = 0;
    virtual void visit_unary(UnaryExpr& expr) = 0;
};

// a literal value straight from the source: 42, 3.14, "hi", true, nil, ...
// we don't have a runtime Value type yet (that comes later), so for now just
// hang on to the token the lexer handed us and let a later pass turn it into
// an actual value.
class LiteralExpr : public Expr {
public:
    explicit LiteralExpr(Token tok) : token(std::move(tok)) {}

    void accept(Visitor& visitor) override { visitor.visit_literal(*this); }

    Token token;
};

// a bare name reference like `x` or `count`. keep the token around as well so
// later passes can point at the right spot when something goes wrong.
class IdentifierExpr : public Expr {
public:
    explicit IdentifierExpr(Token tok) : name(tok.lexeme), token(std::move(tok)) {}

    void accept(Visitor& visitor) override { visitor.visit_identifier(*this); }

    std::string name;
    Token token;
};

// something like `a + b` or `x < 10`. holds both sides plus the operator token
// the lexer gave us so a later pass can look at op.kind to decide what to do.
// we own the children through unique_ptr — when a BinaryExpr dies the whole
// subtree under it goes with it.
class BinaryExpr : public Expr {
public:
    BinaryExpr(std::unique_ptr<Expr> lhs, Token oper, std::unique_ptr<Expr> rhs)
        : left(std::move(lhs)), op(std::move(oper)), right(std::move(rhs)) {}

    void accept(Visitor& visitor) override { visitor.visit_binary(*this); }

    std::unique_ptr<Expr> left;
    Token op;
    std::unique_ptr<Expr> right;
};

// a prefix operator applied to one operand: `-x`, `!done`. same idea as the
// binary node but with a single child.
class UnaryExpr : public Expr {
public:
    UnaryExpr(Token oper, std::unique_ptr<Expr> rhs)
        : op(std::move(oper)), operand(std::move(rhs)) {}

    void accept(Visitor& visitor) override { visitor.visit_unary(*this); }

    Token op;
    std::unique_ptr<Expr> operand;
};

} // namespace brewc

#endif
