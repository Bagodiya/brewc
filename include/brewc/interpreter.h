#ifndef BREWC_INTERPRETER_H
#define BREWC_INTERPRETER_H

#include <memory>
#include <vector>

#include "brewc/ast.h"
#include "brewc/environment.h"
#include "brewc/runtime_error.h"
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
class Interpreter : public Visitor, public StmtVisitor {
public:
    Interpreter();

    // run a whole program, one top-level statement after another.
    void interpret(std::vector<std::unique_ptr<Stmt>>& program);

    void visit_literal(LiteralExpr& expr) override;
    void visit_identifier(IdentifierExpr& expr) override;
    void visit_binary(BinaryExpr& expr) override;
    void visit_unary(UnaryExpr& expr) override;
    void visit_call(CallExpr& expr) override;
    void visit_assign(AssignExpr& expr) override;

    void visit_let(LetStmt& stmt) override;
    void visit_if(IfStmt& stmt) override;
    void visit_while(WhileStmt& stmt) override;
    void visit_block(BlockStmt& stmt) override;
    void visit_fn(FnDecl& stmt) override;
    void visit_expr_stmt(ExprStmt& stmt) override;
    void visit_return(ReturnStmt& stmt) override;

    // walk one expression and hand back whatever it evaluated to. internally it
    // runs accept() and then reads result_, which keeps the visit_* bodies from
    // having to touch the shared slot by hand. it's public so the REPL and the
    // tests can evaluate a single expression without running a whole program.
    Value evaluate(Expr& expr);

    // run one statement for its effect. thin wrapper around accept() so the
    // statement side reads the same way the expression side does. public for the
    // same reason evaluate() is: the repl runs a line a statement at a time so it
    // can print the value of a bare expression, and wrapping each one in a
    // throwaway vector just to reach interpret() would be silly.
    void execute(Stmt& stmt);

private:
    // drop the builtin functions (print, and more later) into the global scope so
    // a program can call them by name without declaring them first.
    void register_builtins();

    // bail out of the run with a RuntimeError. it stamps the message with the
    // source position from `where` and takes a snapshot of the call stack as it
    // stands right now, so whoever catches it can print where things went wrong
    // and how we got there. marked noreturn because it always throws.
    [[noreturn]] void fail(const std::string& message, const Token& where);

    // the outermost scope, alive for the whole run. it's a shared_ptr now so a
    // function declared up here can close over it the same way one declared inside
    // a block closes over its block — every scope is heap-owned, no special case
    // for the globals.
    std::shared_ptr<Environment> globals_;
    // the scope we're binding and looking names up in right now. it starts at the
    // globals and a block temporarily points it at a nested scope while it runs,
    // then puts it back.
    std::shared_ptr<Environment> current_;
    Value result_ = Nil{}; // the most recent expression result, ferried out of visit_*

    // the functions we're currently inside, oldest at the front. a call pushes a
    // frame before it runs the body and pops it after, so at any moment this is
    // the live stack an error can copy into its trace.
    std::vector<TraceFrame> call_stack_;
};

} // namespace brewc

#endif
