#ifndef BREWC_AST_H
#define BREWC_AST_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

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
class CallExpr;

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
    virtual void visit_call(CallExpr& expr) = 0;
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

// a function call like `foo(1, 2)`. the callee is itself an expression — usually
// just an identifier, but keeping it as an Expr means `get_fn()(x)` style calls
// work later without changing this node. we own the callee and every argument.
// the paren token is handy for pointing at the call site in error messages.
class CallExpr : public Expr {
public:
    CallExpr(std::unique_ptr<Expr> target, Token paren_tok,
             std::vector<std::unique_ptr<Expr>> arg_list)
        : callee(std::move(target)), paren(std::move(paren_tok)),
          args(std::move(arg_list)) {}

    void accept(Visitor& visitor) override { visitor.visit_call(*this); }

    std::unique_ptr<Expr> callee;
    Token paren;
    std::vector<std::unique_ptr<Expr>> args;
};

// statements are the other half of the tree. expressions produce a value;
// statements get run for their effect (binding a name, looping, ...). they get
// their own base class and their own visitor so a pass can choose to care about
// only one side or the other.
class StmtVisitor;

class Stmt {
public:
    virtual ~Stmt() = default;
    virtual void accept(StmtVisitor& visitor) = 0;
};

// forward declare each concrete statement so StmtVisitor can name it. more get
// added here as the later steps bring in functions.
class LetStmt;
class IfStmt;
class WhileStmt;
class BlockStmt;
class FnDecl;

class StmtVisitor {
public:
    virtual ~StmtVisitor() = default;
    virtual void visit_let(LetStmt& stmt) = 0;
    virtual void visit_if(IfStmt& stmt) = 0;
    virtual void visit_while(WhileStmt& stmt) = 0;
    virtual void visit_block(BlockStmt& stmt) = 0;
    virtual void visit_fn(FnDecl& stmt) = 0;
};

// `let x = <expr>`. we keep the name token (so error messages can point at the
// right spot) and own the initializer expression. the initializer is always
// present for now — bare `let x;` isn't part of the grammar yet.
class LetStmt : public Stmt {
public:
    LetStmt(Token name_tok, std::unique_ptr<Expr> init)
        : name(name_tok.lexeme), name_token(std::move(name_tok)),
          initializer(std::move(init)) {}

    void accept(StmtVisitor& visitor) override { visitor.visit_let(*this); }

    std::string name;
    Token name_token;
    std::unique_ptr<Expr> initializer;
};

// a sequence of statements that share a scope: `{ ... }`. owns each statement
// inside it. we put this above IfStmt/WhileStmt since their branches are
// usually blocks and it reads nicer to have it defined first.
class BlockStmt : public Stmt {
public:
    explicit BlockStmt(std::vector<std::unique_ptr<Stmt>> stmts)
        : statements(std::move(stmts)) {}

    void accept(StmtVisitor& visitor) override { visitor.visit_block(*this); }

    std::vector<std::unique_ptr<Stmt>> statements;
};

// `if <cond> { ... } else { ... }`. the else side is optional, so else_branch
// is allowed to be null. branches are just statements (normally a BlockStmt).
class IfStmt : public Stmt {
public:
    IfStmt(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> then_b,
           std::unique_ptr<Stmt> else_b)
        : condition(std::move(cond)), then_branch(std::move(then_b)),
          else_branch(std::move(else_b)) {}

    void accept(StmtVisitor& visitor) override { visitor.visit_if(*this); }

    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> then_branch;
    std::unique_ptr<Stmt> else_branch; // may be null when there's no else
};

// `while <cond> { ... }`. runs the body as long as the condition holds.
class WhileStmt : public Stmt {
public:
    WhileStmt(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> body_stmt)
        : condition(std::move(cond)), body(std::move(body_stmt)) {}

    void accept(StmtVisitor& visitor) override { visitor.visit_while(*this); }

    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;
};

// `fn name(a, b) { ... }`. params are kept as tokens so we still have their
// source location and lexeme; a later pass turns those into the actual bindings.
// the body is whatever block the parser hands us, owned here. an empty params
// vector just means a no-arg function like `fn main() { ... }`.
class FnDecl : public Stmt {
public:
    FnDecl(Token name_tok, std::vector<Token> param_list, std::unique_ptr<Stmt> body_stmt)
        : name(name_tok.lexeme), name_token(std::move(name_tok)),
          params(std::move(param_list)), body(std::move(body_stmt)) {}

    void accept(StmtVisitor& visitor) override { visitor.visit_fn(*this); }

    std::string name;
    Token name_token;
    std::vector<Token> params;
    std::unique_ptr<Stmt> body;
};

} // namespace brewc

#endif
