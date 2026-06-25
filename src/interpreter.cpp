#include "brewc/interpreter.h"

#include <stdexcept>
#include <string>

namespace brewc {

namespace {

// the int and float cases share the same shape, so each gets its own little
// helper instead of one big switch that has to keep re-checking the operand
// types. division and modulo by zero on ints would be undefined behaviour, so
// those two are guarded; the float side just lets IEEE produce inf/nan.
int64_t apply_int(TokenKind op, int64_t a, int64_t b) {
    switch (op) {
    case TokenKind::Plus:
        return a + b;
    case TokenKind::Minus:
        return a - b;
    case TokenKind::Star:
        return a * b;
    case TokenKind::Slash:
        if (b == 0) {
            throw std::runtime_error("division by zero");
        }
        return a / b;
    case TokenKind::Percent:
        if (b == 0) {
            throw std::runtime_error("modulo by zero");
        }
        return a % b;
    default:
        throw std::runtime_error("operator is not arithmetic");
    }
}

double apply_float(TokenKind op, double a, double b) {
    switch (op) {
    case TokenKind::Plus:
        return a + b;
    case TokenKind::Minus:
        return a - b;
    case TokenKind::Star:
        return a * b;
    case TokenKind::Slash:
        return a / b;
    default:
        // there's no sensible float modulo here, and that's the only operator
        // that falls through to this point.
        throw std::runtime_error("operator is not valid on floats");
    }
}

} // namespace

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
// steps. the casts to void just keep -Wunused-parameter quiet until then.

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

// evaluate both sides first, then run the operator. arithmetic only makes sense
// on numbers right now, so two ints give an int and two floats give a float.
// mixing the two (or throwing a string/bool in) is an error for now — promotion
// and string handling land in their own later steps.
void Interpreter::visit_binary(BinaryExpr& expr) {
    Value lhs = evaluate(*expr.left);
    Value rhs = evaluate(*expr.right);

    if (is_int(lhs) && is_int(rhs)) {
        result_ = apply_int(expr.op.kind, std::get<int64_t>(lhs), std::get<int64_t>(rhs));
        return;
    }
    if (is_float(lhs) && is_float(rhs)) {
        result_ = apply_float(expr.op.kind, std::get<double>(lhs), std::get<double>(rhs));
        return;
    }

    throw std::runtime_error("cannot apply '" + expr.op.lexeme + "' to " + type_name(lhs) +
                             " and " + type_name(rhs));
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
