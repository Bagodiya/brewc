#ifndef BREWC_INTERPRETER_H
#define BREWC_INTERPRETER_H

#include <memory>
#include <vector>

#include "brewc/ast.h"
#include "brewc/environment.h"
#include "brewc/value.h"

namespace brewc {

// the tree-walking interpreter. it just visits the AST directly and runs each
// node as it goes — no bytecode, no separate compile step. that comes later in
// the project; this is the simple version that gets programs running first.
//
// like the printer it implements both visitor interfaces because the tree has
// expressions and statements mixed together. the visit_* methods return void,
// so when an expression produces a value we stash it in result_ and the caller
// pulls it back out through evaluate(). globals_ is the outermost scope and
// stays alive for the whole run.
//
// the visit_* bodies are empty stubs for now — the next handful of steps fill
// them in one node type at a time.
class Interpreter : public Visitor, public StmtVisitor {
public:
    Interpreter() = default;

    // run a whole program, one top-level statement after another.
    void interpret(std::vector<std::unique_ptr<Stmt>>& program);

    void visit_literal(LiteralExpr& expr) override;
    void visit_identifier(IdentifierExpr& expr) override;
    void visit_binary(BinaryExpr& expr) override;
    void visit_unary(UnaryExpr& expr) override;
    void visit_call(CallExpr& expr) override;

    void visit_let(LetStmt& stmt) override;
    void visit_if(IfStmt& stmt) override;
    void visit_while(WhileStmt& stmt) override;
    void visit_block(BlockStmt& stmt) override;
    void visit_fn(FnDecl& stmt) override;

    // walk one expression and hand back whatever it evaluated to. internally it
    // runs accept() and then reads result_, which keeps the visit_* bodies from
    // having to touch the shared slot by hand. it's public so the REPL and the
    // tests can evaluate a single expression without running a whole program.
    Value evaluate(Expr& expr);

private:
    // run one statement for its effect. thin wrapper around accept() so the
    // statement side reads the same way the expression side does.
    void execute(Stmt& stmt);

    Environment globals_;
    // the scope we're binding and looking names up in right now. it starts at the
    // globals and a block temporarily points it at a nested scope while it runs,
    // then puts it back. declared after globals_ so &globals_ is valid here.
    Environment* current_ = &globals_;
    Value result_ = Nil{}; // the most recent expression result, ferried out of visit_*
};

} // namespace brewc

#endif
