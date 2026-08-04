#include "brewc/ast_printer.h"

namespace brewc {

// run one node through accept() and pull back whatever it wrote. having this
// return the string keeps the visit_* bodies readable, they can just splice
// child(...) into the parens instead of juggling the shared buffer by hand.
namespace {

std::string child(AstPrinter& printer, Expr& expr) {
    return printer.print(expr);
}

std::string child(AstPrinter& printer, Stmt& stmt) {
    return printer.print(stmt);
}

} // namespace

std::string AstPrinter::print(Expr& expr) {
    expr.accept(*this);
    return buf_;
}

std::string AstPrinter::print(Stmt& stmt) {
    stmt.accept(*this);
    return buf_;
}

void AstPrinter::visit_literal(LiteralExpr& expr) {
    buf_ = expr.token.lexeme;
}

void AstPrinter::visit_identifier(IdentifierExpr& expr) {
    buf_ = expr.name;
}

void AstPrinter::visit_binary(BinaryExpr& expr) {
    std::string l = child(*this, *expr.left);
    std::string r = child(*this, *expr.right);
    buf_ = "(" + expr.op.lexeme + " " + l + " " + r + ")";
}

void AstPrinter::visit_unary(UnaryExpr& expr) {
    std::string inner = child(*this, *expr.operand);
    buf_ = "(" + expr.op.lexeme + " " + inner + ")";
}

void AstPrinter::visit_call(CallExpr& expr) {
    std::string s = "(call " + child(*this, *expr.callee);
    for (auto& arg : expr.args) {
        s += " " + child(*this, *arg);
    }
    buf_ = s + ")";
}

void AstPrinter::visit_assign(AssignExpr& expr) {
    buf_ = "(= " + expr.name + " " + child(*this, *expr.value) + ")";
}

void AstPrinter::visit_expr_stmt(ExprStmt& stmt) {
    // no wrapper of its own — an expression statement is just the expression, and
    // printing `(expr (call print x))` would only add noise to the snapshots.
    buf_ = child(*this, *stmt.expr);
}

void AstPrinter::visit_return(ReturnStmt& stmt) {
    if (!stmt.value) {
        buf_ = "(return)";
        return;
    }
    buf_ = "(return " + child(*this, *stmt.value) + ")";
}

void AstPrinter::visit_let(LetStmt& stmt) {
    buf_ = "(let " + stmt.name + " " + child(*this, *stmt.initializer) + ")";
}

void AstPrinter::visit_if(IfStmt& stmt) {
    std::string s = "(if " + child(*this, *stmt.condition);
    s += " " + child(*this, *stmt.then_branch);
    if (stmt.else_branch) {
        s += " " + child(*this, *stmt.else_branch);
    }
    buf_ = s + ")";
}

void AstPrinter::visit_while(WhileStmt& stmt) {
    std::string cond = child(*this, *stmt.condition);
    std::string body = child(*this, *stmt.body);
    buf_ = "(while " + cond + " " + body + ")";
}

void AstPrinter::visit_block(BlockStmt& stmt) {
    std::string s = "(block";
    for (auto& inner : stmt.statements) {
        s += " " + child(*this, *inner);
    }
    buf_ = s + ")";
}

void AstPrinter::visit_fn(FnDecl& stmt) {
    std::string s = "(fn " + stmt.name + " (";
    for (std::size_t i = 0; i < stmt.params.size(); ++i) {
        if (i != 0) {
            s += " ";
        }
        s += stmt.params[i].lexeme;
    }
    buf_ = s + ") " + child(*this, *stmt.body) + ")";
}

} // namespace brewc
