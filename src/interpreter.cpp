#include "brewc/interpreter.h"

namespace brewc {

void Interpreter::interpret(std::vector<std::unique_ptr<Stmt>>& program) {
    for (auto& stmt : program) {
        if (stmt) {
            execute(*stmt);
        }
    }
}

Value Interpreter::evaluate(Expr& expr) {
    expr.accept(*this);
    return result_;
}

void Interpreter::execute(Stmt& stmt) {
    stmt.accept(*this);
}

// everything below is a stub for now. the visit_* methods get real bodies over
// the next few steps (literals first, then arithmetic, then the rest). the
// casts to void just keep -Wunused-parameter quiet until then.

void Interpreter::visit_literal(LiteralExpr& expr) {
    (void)expr;
}

void Interpreter::visit_identifier(IdentifierExpr& expr) {
    (void)expr;
}

void Interpreter::visit_binary(BinaryExpr& expr) {
    (void)expr;
}

void Interpreter::visit_unary(UnaryExpr& expr) {
    (void)expr;
}

void Interpreter::visit_call(CallExpr& expr) {
    (void)expr;
}

void Interpreter::visit_let(LetStmt& stmt) {
    (void)stmt;
}

void Interpreter::visit_if(IfStmt& stmt) {
    (void)stmt;
}

void Interpreter::visit_while(WhileStmt& stmt) {
    (void)stmt;
}

void Interpreter::visit_block(BlockStmt& stmt) {
    (void)stmt;
}

void Interpreter::visit_fn(FnDecl& stmt) {
    (void)stmt;
}

} // namespace brewc
