#include "brewc/interpreter.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "brewc/value_ops.h"

namespace brewc {

namespace {

// the print builtin. walk the arguments left to right and write each one out with
// to_string, which already knows how to render every Value variant, so this works
// on ints, floats, bools, strings, nil and even functions without any extra work
// here. multiple arguments are separated by a single space and the whole line ends
// with a newline. there's nothing useful to return, so a call to print comes back
// as nil like any other side-effecting statement.
Value builtin_print(const std::vector<Value>& args) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            std::cout << ' ';
        }
        std::cout << to_string(args[i]);
    }
    std::cout << '\n';
    return Nil{};
}

// clock() hands back a number of seconds so a program can time itself: grab it
// once before the work and once after, then subtract. steady_clock is the right
// pick here because it never jumps around the way the wall clock can when the
// system time gets adjusted, so a difference between two readings is always a
// real elapsed duration. the zero point is arbitrary, but that doesn't matter
// since only the gap between two calls is ever meaningful. any arguments are
// ignored — clock doesn't take any.
Value builtin_clock(const std::vector<Value>& args) {
    (void)args;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    double seconds = std::chrono::duration<double>(now).count();
    return seconds;
}

// how a `return` gets from wherever it was written back out to the call that
// started the body. it can be nested any number of blocks, ifs and loops deep, so
// there's nothing to check on the way out — throwing unwinds all of it at once and
// visit_call catches it at the boundary.
//
// this is control flow, not a failure, so it deliberately doesn't derive from
// RuntimeError or std::exception. the catch(...) blocks that restore scopes still
// see it and put current_ back, but nothing that reports errors can mistake it for
// one, and a `return` can never be caught by a handler looking for a real error.
struct ReturnSignal {
    Value value;
};

} // namespace

// spin up the global scope on the heap and point current_ at it to start. both
// are shared_ptrs so any function declared later can grab a handle and keep the
// scope it was born in alive for as long as the function value sticks around.
Interpreter::Interpreter()
    : globals_(std::make_shared<Environment>()), current_(globals_) {
    register_builtins();
}

// build each builtin value and bind it in the globals so a program can reach them
// by name without declaring anything. print writes values out; clock is for timing.
void Interpreter::register_builtins() {
    auto print = std::make_shared<NativeFn>();
    print->name = "print";
    print->call = builtin_print;
    globals_->define("print", print);

    auto clock = std::make_shared<NativeFn>();
    clock->name = "clock";
    clock->call = builtin_clock;
    globals_->define("clock", clock);
}

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

// every runtime failure funnels through here. we copy call_stack_ as it stands
// so the trace freezes the calls that were still open, then throw. the copy
// matters: the frames get popped as the exception unwinds back up, so grabbing
// them now is the only chance to keep the full chain.
void Interpreter::fail(const std::string& message, const Token& where) {
    throw RuntimeError(message, where.line, where.column, call_stack_);
}

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
        fail("undefined variable '" + expr.name + "'", expr.token);
    }
    result_ = *slot;
}

// evaluate both sides first, then run the operator. arithmetic only makes sense
// on numbers right now: two ints give an int, and anything with a float in it
// gives a float. `+` does double duty — two strings glue together instead.
void Interpreter::visit_binary(BinaryExpr& expr) {
    // && and || have to be dealt with before anything else, because they don't
    // always evaluate their right side. `false && f()` never calls f, and that's
    // not an optimisation — it's what lets you write a guard like
    // `n != 0 && total / n > 1` and rely on the left side to protect the right.
    // every other operator below evaluates both sides up front, which would
    // defeat that, so these two can't share the path.
    if (expr.op.kind == TokenKind::AmpAmp || expr.op.kind == TokenKind::PipePipe) {
        bool left_true = is_truthy(evaluate(*expr.left));

        // once the left side settles it, the answer is already known and the
        // right side is left alone: false wins for &&, true wins for ||.
        if (expr.op.kind == TokenKind::AmpAmp ? !left_true : left_true) {
            result_ = left_true;
            return;
        }

        // the result comes back as a bool rather than as the operand itself, so
        // `1 && 2` is true, not 2. that matches the comparisons, which always
        // hand back a bool no matter what they were given.
        result_ = is_truthy(evaluate(*expr.right));
        return;
    }

    Value lhs = evaluate(*expr.left);
    Value rhs = evaluate(*expr.right);

    // the helpers below throw a bare message when something's off (divide by
    // zero, comparing things that can't be ordered, ...). they don't know where in
    // the source we are, so catch that here where we still have the operator token
    // and re-throw it through fail() with the location and stack attached. a nested
    // evaluate() above already ran outside this try, so its own errors keep their
    // own position instead of getting stamped with this operator's.
    try {
        // comparisons split off here before the arithmetic paths, since they always
        // come back as a bool no matter what the operands were.
        if (is_comparison(expr.op.kind)) {
            result_ = compare(expr.op.kind, lhs, rhs);
            return;
        }

        // "he" + "llo" makes a new string. only + means anything between two
        // strings, so - or * on them drops through to the error at the bottom
        // the same as any other bad pairing.
        if (is_string(lhs) && is_string(rhs) && expr.op.kind == TokenKind::Plus) {
            result_ = std::get<std::string>(lhs) + std::get<std::string>(rhs);
            return;
        }

        if (is_int(lhs) && is_int(rhs)) {
            result_ = apply_int(expr.op.kind, std::get<int64_t>(lhs), std::get<int64_t>(rhs));
            return;
        }

        // anything else numeric ends up here: float with float, or one of each. the
        // int side gets widened and the float rules take over, so 1 + 2.5 is 3.5.
        // the answer is a float even when it lands on a whole number, since 1 + 1.0
        // giving back an int would quietly throw away the .0 the user wrote.
        if (is_number(lhs) && is_number(rhs)) {
            result_ = apply_float(expr.op.kind, to_double(lhs), to_double(rhs));
            return;
        }
    } catch (const std::runtime_error& e) {
        fail(e.what(), expr.op);
    }

    fail("cannot apply '" + expr.op.lexeme + "' to " + type_name(lhs) + " and " + type_name(rhs),
         expr.op);
}

// `-x` and `!x`. run the operand first, then apply the operator to whatever came
// back. negation only means anything on a number, so anything else stops the run
// rather than guessing; `!` works on every value because truthiness is defined
// for all of them.
void Interpreter::visit_unary(UnaryExpr& expr) {
    Value operand = evaluate(*expr.operand);

    switch (expr.op.kind) {
    case TokenKind::Minus:
        if (is_int(operand)) {
            // negating the most negative int64 has no positive twin to land on,
            // and writing -v for that value is undefined behaviour rather than
            // the wraparound you'd expect. going through the unsigned type makes
            // the wrap well defined, and the result is the same everywhere else.
            std::uint64_t bits = static_cast<std::uint64_t>(std::get<int64_t>(operand));
            result_ = static_cast<int64_t>(0u - bits);
            return;
        }
        if (is_float(operand)) {
            result_ = -std::get<double>(operand);
            return;
        }
        fail("cannot negate " + type_name(operand), expr.op);

    case TokenKind::Bang:
        // `!` always comes back as a bool, whatever went in, so it lines up with
        // the same truthiness rule if and while use.
        result_ = !is_truthy(operand);
        return;

    default:
        // the parser only ever builds a UnaryExpr for those two, so this is only
        // reachable if a new prefix operator gets added and forgotten about here.
        fail("cannot apply '" + expr.op.lexeme + "' as a prefix operator", expr.op);
    }
}

// `x = <expr>`. evaluate the value, then hand it to assign(), which walks outward
// looking for a binding that already exists. a name that was never bound is an
// error rather than a new global — inventing variables is `let`'s job, and a typo
// in an assignment should say so instead of silently making a second variable.
// the assigned value is also the value of the whole expression.
void Interpreter::visit_assign(AssignExpr& expr) {
    Value value = evaluate(*expr.value);
    if (!current_->assign(expr.name, value)) {
        fail("undefined variable '" + expr.name + "'", expr.token);
    }
    result_ = std::move(value);
}

// `foo(1, 2)`. work out what we're calling, make sure it really is a function,
// then line the argument values up against the parameter names. the params go
// into a brand new scope that hangs off the scope the function was declared in,
// not off whoever happened to make the call. that captured scope is what lets the
// body find the function's own name, so a function can call itself and recurse.
// the call evaluates to whatever the body returned, or nil if it ran all the way
// to the end without hitting a `return`.
void Interpreter::visit_call(CallExpr& expr) {
    Value callee = evaluate(*expr.callee);

    // evaluate every argument up front, left to right, before any of them get
    // bound. that way a later argument can't see a half-built call scope. this runs
    // ahead of the callable check so both builtins and user functions get the same
    // ready-made argument list.
    std::vector<Value> arguments;
    arguments.reserve(expr.args.size());
    for (auto& arg : expr.args) {
        arguments.push_back(evaluate(*arg));
    }

    // a builtin has no AST body to walk, so it never touches the scope machinery
    // below — just call straight into the C++ and take whatever it returns.
    if (is_native(callee)) {
        result_ = std::get<std::shared_ptr<NativeFn>>(callee)->call(arguments);
        return;
    }

    if (!is_function(callee)) {
        fail("can only call functions, not " + type_name(callee), expr.paren);
    }
    Function fn = std::get<Function>(callee);
    FnDecl* decl = fn.decl;

    if (arguments.size() != decl->params.size()) {
        fail("function '" + decl->name + "' takes " + std::to_string(decl->params.size()) +
                 " arguments but got " + std::to_string(arguments.size()),
             expr.paren);
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
    // still leaves current_ pointing back at the caller's scope. the trace frame
    // rides along with it: we push before the body runs and pop after, and the
    // catch pops too so an error unwinding through here doesn't leave a stale frame
    // behind (the RuntimeError already grabbed its own copy of the stack anyway).
    std::shared_ptr<Environment> outer = current_;
    current_ = call_scope;
    call_stack_.push_back(TraceFrame{decl->name, expr.paren.line});

    // a body that runs off the end without returning gives back nil, so start
    // there and let a ReturnSignal overwrite it if one shows up.
    Value returned = Nil{};
    try {
        execute(*decl->body);
    } catch (ReturnSignal& signal) {
        // this is the boundary the throw was aimed at. catching it here is what
        // stops it at this call instead of unwinding into the caller, so an inner
        // function returning doesn't also return out of the one that called it.
        returned = std::move(signal.value);
    } catch (...) {
        call_stack_.pop_back();
        current_ = outer;
        throw;
    }
    call_stack_.pop_back();
    current_ = outer;

    result_ = std::move(returned);
}

// an expression standing on its own as a statement. run it and drop the value —
// what we're after is whatever it did along the way, like the output from a
// `print(x)` call or the store from an assignment. the result still lands in
// result_, but nothing reads it before the next statement overwrites it.
void Interpreter::visit_expr_stmt(ExprStmt& stmt) {
    evaluate(*stmt.expr);
}

// `return <expr>`. work out the value first, then throw it at the call that's
// waiting for it. a bare `return` has no expression and comes back as nil, same
// as falling off the end of the body.
//
// the call stack being empty means nothing is waiting to catch this, so the
// `return` is sitting at the top level of the program rather than inside a
// function. that's a mistake worth reporting: letting it through would unwind
// out of interpret() and end the run with no explanation.
void Interpreter::visit_return(ReturnStmt& stmt) {
    if (call_stack_.empty()) {
        fail("'return' outside of a function", stmt.keyword);
    }

    Value value = Nil{};
    if (stmt.value) {
        value = evaluate(*stmt.value);
    }
    throw ReturnSignal{std::move(value)};
}

// `let x = expr` runs the initializer first and then binds the result under the
// name. define() puts the name in this scope even if it was already there, so
// writing `let` twice just replaces the old binding, and a `let` inside a block
// shadows an outer name of the same spelling rather than overwriting it.
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
