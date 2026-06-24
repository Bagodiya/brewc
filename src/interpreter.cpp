#include "brewc/interpreter.h"

#include <string>

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

// the rest are still stubs for now. they get real bodies over the next few
// steps (arithmetic next, then the rest). the casts to void just keep
// -Wunused-parameter quiet until then.

// turn a literal token into the runtime value it stands for. the lexer already
// did the hard part: number tokens carry the digits as their lexeme and string
// tokens carry the text with the quotes and escapes already sorted out, so here
// we just pick the right variant and parse the number when there is one.
void Interpreter::visit_literal(LiteralExpr& expr) {
    switch (expr.token.kind) {
    case TokenKind::Integer:
        result_ = static_cast<int64_t>(std::stoll(expr.token.lexeme));
        break;
    case TokenKind::Float:
        result_ = std::stod(expr.token.lexeme);
        break;
    case TokenKind::String:
        result_ = expr.token.lexeme;
        break;
    case TokenKind::True:
        result_ = true;
        break;
    case TokenKind::False:
        result_ = false;
        break;
    case TokenKind::Nil:
    default:
        // anything else here is a nil literal. nothing else should reach a
        // LiteralExpr, but defaulting to nil keeps the switch total.
        result_ = Nil{};
        break;
    }
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
