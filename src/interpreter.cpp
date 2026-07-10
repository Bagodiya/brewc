#include "brewc/interpreter.h"

#include <cstddef>
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

// spin up the global scope on the heap and point current_ at it to start. both
// are shared_ptrs so any function declared later can grab a handle and keep the
// scope it was born in alive for as long as the function value sticks around.
Interpreter::Interpreter()
    : globals_(std::make_shared<Environment>()), current_(globals_) {}

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
    Value* slot = current_->get(expr.name);
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

// `foo(1, 2)`. work out what we're calling, make sure it really is a function,
// then line the argument values up against the parameter names. the params go
// into a brand new scope that hangs off the scope the function was declared in,
// not off whoever happened to make the call. that captured scope is what lets the
// body find the function's own name, so a function can call itself and recurse.
// there's no return statement in the language yet, so the call just runs the body
// for its effects and comes back as nil.
void Interpreter::visit_call(CallExpr& expr) {
    Value callee = evaluate(*expr.callee);
    if (!is_function(callee)) {
        throw std::runtime_error("can only call functions, not " + type_name(callee));
    }
    Function fn = std::get<Function>(callee);
    FnDecl* decl = fn.decl;

    // evaluate every argument up front, left to right, before any of them get
    // bound. that way a later argument can't see a half-built call scope.
    std::vector<Value> arguments;
    arguments.reserve(expr.args.size());
    for (auto& arg : expr.args) {
        arguments.push_back(evaluate(*arg));
    }

    if (arguments.size() != decl->params.size()) {
        throw std::runtime_error("function '" + decl->name + "' takes " +
                                 std::to_string(decl->params.size()) + " arguments but got " +
                                 std::to_string(arguments.size()));
    }

    // hang the call scope off the scope the function was declared in — its
    // closure. older function values built before we tracked that don't carry one,
    // so fall back to the globals in that case.
    std::shared_ptr<Environment> enclosing = fn.closure ? fn.closure : globals_;
    auto call_scope = std::make_shared<Environment>(enclosing);
    for (std::size_t i = 0; i < decl->params.size(); ++i) {
        call_scope->define(decl->params[i].lexeme, arguments[i]);
    }

    // same save/restore dance the block does, so a throw partway through the body
    // still leaves current_ pointing back at the caller's scope.
    std::shared_ptr<Environment> outer = current_;
    current_ = call_scope;
    try {
        execute(*decl->body);
    } catch (...) {
        current_ = outer;
        throw;
    }
    current_ = outer;

    result_ = Nil{};
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
    current_->define(stmt.name, std::move(value));
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

// a block runs its statements in a fresh scope hung off the current one, so any
// name a `let` binds inside the braces is gone again once we leave. we point
// current_ at the new scope while the block runs and set it back after. the
// try/catch is only there so that if a statement throws partway through we still
// restore the outer scope instead of leaving current_ aimed at a scope that's
// already been destroyed.
void Interpreter::visit_block(BlockStmt& stmt) {
    auto inner = std::make_shared<Environment>(current_);
    std::shared_ptr<Environment> outer = current_;
    current_ = inner;
    try {
        for (auto& s : stmt.statements) {
            if (s) {
                execute(*s);
            }
        }
    } catch (...) {
        current_ = outer;
        throw;
    }
    current_ = outer;
}

// `fn name(...) { ... }` just binds a value like a let does — the value happens
// to be callable. we hand the binding a Function pointing back at this decl and
// drop it into the current scope under the function's name. no call happens here;
// this only makes the name resolve to something. actually running the body is the
// next step's job. binding it as a plain value is what makes functions first
// class: you can look one up, pass it around, and later call it.
void Interpreter::visit_fn(FnDecl& stmt) {
    // remember the scope we're declaring into so a later call can hang its own
    // scope off it. for a top-level fn that's just the globals, but capturing it
    // here is what makes the name resolve to itself once the body runs.
    current_->define(stmt.name, Function{&stmt, current_});
}

} // namespace brewc
