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

bool is_comparison(TokenKind op) {
    switch (op) {
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual:
    case TokenKind::Less:
    case TokenKind::Greater:
    case TokenKind::LessEqual:
    case TokenKind::GreaterEqual:
        return true;
    default:
        return false;
    }
}

// all six comparisons for one number type. ints and floats both run through here
// because the operators mean the same thing for either, only the T changes.
template <typename T>
bool compare_numbers(TokenKind op, T a, T b) {
    switch (op) {
    case TokenKind::EqualEqual:
        return a == b;
    case TokenKind::BangEqual:
        return a != b;
    case TokenKind::Less:
        return a < b;
    case TokenKind::Greater:
        return a > b;
    case TokenKind::LessEqual:
        return a <= b;
    case TokenKind::GreaterEqual:
        return a >= b;
    default:
        throw std::runtime_error("operator is not a comparison");
    }
}

// what counts as "true" when a value lands in an if/while condition. only nil
// and a false bool are falsy; everything else is true, including zero and the
// empty string. keeping the rule this small means there's nothing to memorize.
bool is_truthy(const Value& value) {
    if (is_nil(value)) {
        return false;
    }
    if (is_bool(value)) {
        return std::get<bool>(value);
    }
    return true;
}

// figure out a == b for the non-number cases. only two values of the exact same
// kind can be equal, so a bool is never equal to a string and so on. nil only
// ever equals nil.
bool values_equal(const Value& a, const Value& b) {
    if (is_bool(a) && is_bool(b)) {
        return std::get<bool>(a) == std::get<bool>(b);
    }
    if (is_string(a) && is_string(b)) {
        return std::get<std::string>(a) == std::get<std::string>(b);
    }
    if (is_nil(a) && is_nil(b)) {
        return true;
    }
    return false;
}

// run a comparison operator and hand back a bool. numbers can use any of the
// six; everything else only gets == and !=, since ordering strings or bools
// isn't something the language promises yet.
bool compare(TokenKind op, const Value& lhs, const Value& rhs) {
    if (is_int(lhs) && is_int(rhs)) {
        return compare_numbers(op, std::get<int64_t>(lhs), std::get<int64_t>(rhs));
    }
    if (is_float(lhs) && is_float(rhs)) {
        return compare_numbers(op, std::get<double>(lhs), std::get<double>(rhs));
    }

    if (op == TokenKind::EqualEqual) {
        return values_equal(lhs, rhs);
    }
    if (op == TokenKind::BangEqual) {
        return !values_equal(lhs, rhs);
    }

    throw std::runtime_error("cannot order " + type_name(lhs) + " and " + type_name(rhs));
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

// read a variable back out. we look it up in the current scope chain and copy
// the stored value into result_. if the name was never bound the get() call
// hands back a null slot, which means the program referred to something that
// doesn't exist, so we stop with an error instead of returning a garbage value.
void Interpreter::visit_identifier(IdentifierExpr& expr) {
    Value* slot = globals_.get(expr.name);
    if (slot == nullptr) {
        throw std::runtime_error("undefined variable '" + expr.name + "'");
    }
    result_ = *slot;
}

// evaluate both sides first, then run the operator. arithmetic only makes sense
// on numbers right now, so two ints give an int and two floats give a float.
// mixing the two (or throwing a string/bool in) is an error for now — promotion
// and string handling land in their own later steps.
void Interpreter::visit_binary(BinaryExpr& expr) {
    Value lhs = evaluate(*expr.left);
    Value rhs = evaluate(*expr.right);

    // comparisons split off here before the arithmetic paths, since they always
    // come back as a bool no matter what the operands were.
    if (is_comparison(expr.op.kind)) {
        result_ = compare(expr.op.kind, lhs, rhs);
        return;
    }

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

// `let x = expr;` runs the initializer first and then binds the result under the
// name. an empty initializer (just `let x;`) starts the variable off as nil so a
// later read still finds something. define() puts the name in this scope even if
// it was already there, so writing `let` twice just replaces the old binding.
void Interpreter::visit_let(LetStmt& stmt) {
    Value value = Nil{};
    if (stmt.initializer) {
        value = evaluate(*stmt.initializer);
    }
    globals_.define(stmt.name, std::move(value));
}

// run the condition, then take exactly one branch. the then side runs when the
// condition is truthy; otherwise the else side runs if there is one. a missing
// else just means a false condition does nothing at all.
void Interpreter::visit_if(IfStmt& stmt) {
    if (is_truthy(evaluate(*stmt.condition))) {
        execute(*stmt.then_branch);
    } else if (stmt.else_branch) {
        execute(*stmt.else_branch);
    }
}

// keep running the body as long as the condition stays truthy. the check happens
// before each pass, so a condition that's already false just runs the body zero
// times. re-checking every time round is what lets the body change something the
// condition looks at and eventually fall out of the loop.
void Interpreter::visit_while(WhileStmt& stmt) {
    while (is_truthy(evaluate(*stmt.condition))) {
        execute(*stmt.body);
    }
}

void Interpreter::visit_block(BlockStmt& stmt) {
    (void)stmt;
}

void Interpreter::visit_fn(FnDecl& stmt) {
    (void)stmt;
}

} // namespace brewc
