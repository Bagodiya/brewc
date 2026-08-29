#include "brewc/compiler.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace brewc {

namespace {

// same "line 2:5: ..." prefix the parser puts on its errors. the two error types
// don't share a base class, but a user reading the output shouldn't be able to
// tell which pass complained from the shape of the message.
std::string with_location(int line, int column, const std::string& message) {
    return "line " + std::to_string(line) + ":" + std::to_string(column) + ": " + message;
}

// the value a literal token stands for. the lexer kept the digits as text and
// nobody has turned them into a number since, so that happens here — the same
// conversion the interpreter does in its own visit_literal, just landing in the
// constant pool instead of in a result slot.
//
// only the kinds that need a pool entry get this far; true, false and nil are
// caught by the switch in visit_literal before it calls here.
Value literal_value(const Token& token) {
    switch (token.kind) {
    case TokenKind::Integer:
        return static_cast<int64_t>(std::stoll(token.lexeme));
    case TokenKind::Float:
        return std::stod(token.lexeme);
    case TokenKind::String:
        return token.lexeme;
    default:
        return Nil{};
    }
}

// the instruction an arithmetic operator compiles down to, or false if this
// isn't one of them. comparisons are handled just below; && and || go through
// the same visit_binary but fall out of both and are reported instead of
// quietly emitting the wrong thing.
bool arithmetic_opcode(TokenKind kind, Opcode& out) {
    switch (kind) {
    case TokenKind::Plus:
        out = Opcode::Add;
        return true;
    case TokenKind::Minus:
        out = Opcode::Sub;
        return true;
    case TokenKind::Star:
        out = Opcode::Mul;
        return true;
    case TokenKind::Slash:
        out = Opcode::Div;
        return true;
    case TokenKind::Percent:
        out = Opcode::Mod;
        return true;
    default:
        return false;
    }
}

// same idea for the comparisons, except two of them need a second instruction
// after the first. `negate` is set when the result has to be flipped with a Not.
//
// <= and >= have no opcode of their own on purpose. `a <= b` is the same thing
// as `not (a > b)`, so emitting Greater and inverting gets the answer without
// two more entries in the enum and two more cases in the VM's dispatch loop
// later. it costs one extra byte per comparison at runtime, which is nothing
// next to keeping the instruction set small.
//
// != could have been folded away the same way, but NotEqual is already in the
// enum, so it stays a single instruction rather than Equal followed by a Not.
bool comparison_opcode(TokenKind kind, Opcode& out, bool& negate) {
    negate = false;
    switch (kind) {
    case TokenKind::EqualEqual:
        out = Opcode::Equal;
        return true;
    case TokenKind::BangEqual:
        out = Opcode::NotEqual;
        return true;
    case TokenKind::Less:
        out = Opcode::Less;
        return true;
    case TokenKind::Greater:
        out = Opcode::Greater;
        return true;
    case TokenKind::LessEqual:
        out = Opcode::Greater;
        negate = true;
        return true;
    case TokenKind::GreaterEqual:
        out = Opcode::Less;
        negate = true;
        return true;
    default:
        return false;
    }
}

} // namespace

CompileError::CompileError(int line, int column, const std::string& message)
    : std::runtime_error(with_location(line, column, message)), line_(line), column_(column) {}

Compiler::Compiler() = default;

Chunk Compiler::compile(std::vector<std::unique_ptr<Stmt>>& program) {
    reset();

    for (std::unique_ptr<Stmt>& stmt : program) {
        compile_stmt(*stmt);
    }

    // nothing in the statement loop emits this, so the top level gets one here.
    // the VM stops when it hits a Return, and a chunk without one would run off
    // the end of the code vector into whatever comes next in memory.
    emit(Opcode::Return);
    return std::move(chunk_);
}

Chunk Compiler::compile_expression(Expr& expr) {
    reset();
    compile_expr(expr);
    emit(Opcode::Return);
    return std::move(chunk_);
}

void Compiler::compile_expr(Expr& expr) { expr.accept(*this); }

void Compiler::compile_stmt(Stmt& stmt) { stmt.accept(*this); }

void Compiler::reset() {
    // moving the chunk out at the end of a compile leaves it in some valid but
    // unspecified state, so assigning a fresh one is the only safe way to reuse
    // the same Compiler for a second program.
    chunk_ = Chunk{};
    line_ = 1;

    // a compile that stopped part way through a block left both of these where it
    // gave up, and starting the next program at depth 2 with someone else's
    // locals still listed would resolve names to slots that hold nothing.
    locals_.clear();
    scope_depth_ = 0;
}

void Compiler::emit(Opcode op) { chunk_.write(op, line_); }

void Compiler::emit(Opcode op, uint8_t operand) {
    emit(op);
    emit_byte(operand);
}

void Compiler::emit_byte(uint8_t byte) { chunk_.write(byte, line_); }

void Compiler::emit_constant(Value value, const Token& where) {
    std::size_t index = chunk_.add_constant(std::move(value));
    if (index >= Chunk::max_constants) {
        fail("too many constants in one chunk (max " + std::to_string(Chunk::max_constants) + ")",
             where);
    }

    // the cast is safe because of the check above — anything the pool accepted
    // has an index below 256, which is the whole reason the limit is there.
    emit(Opcode::Const, static_cast<uint8_t>(index));
}

uint8_t Compiler::name_constant(const std::string& name, const Token& where) {
    std::size_t index = chunk_.add_constant(name);
    if (index >= Chunk::max_constants) {
        fail("too many constants in one chunk (max " + std::to_string(Chunk::max_constants) + ")",
             where);
    }
    return static_cast<uint8_t>(index);
}

void Compiler::begin_scope() { ++scope_depth_; }

void Compiler::end_scope() {
    --scope_depth_;

    // the locals declared inside the block are still on the stack — nothing put
    // them there but the initializer that pushed the value, and nothing has taken
    // them off. so one Pop each on the way out, or the stack would be deeper
    // after the block than before it and every slot number worked out later would
    // be off by however many were left behind.
    //
    // popping from the back is the only order that works, since the innermost
    // local is the one on top.
    while (!locals_.empty() && locals_.back().depth > scope_depth_) {
        emit(Opcode::Pop);
        locals_.pop_back();
    }
}

void Compiler::add_local(const std::string& name, const Token& where) {
    if (locals_.size() >= max_locals) {
        fail("too many local variables in one scope (max " + std::to_string(max_locals) + ")",
             where);
    }

    // a name already used at this depth is not an error. it shadows, and since
    // resolve_local searches backwards the new entry is the one that gets found
    // from here on. Environment::define does the same thing in the tree-walker,
    // so `let x = 1 let x = x + 1` means the same in both backends.
    locals_.push_back(Local{name, scope_depth_});
}

int Compiler::resolve_local(const std::string& name) const {
    for (std::size_t i = locals_.size(); i > 0; --i) {
        if (locals_[i - 1].name == name) {
            return static_cast<int>(i - 1);
        }
    }
    return -1;
}

void Compiler::fail(const std::string& message, const Token& where) {
    throw CompileError(where.line, where.column, message);
}

void Compiler::set_line(const Token& token) { line_ = token.line; }

// a literal turns into one instruction that pushes its value, which keeps the
// "every expression leaves exactly one thing on the stack" rule the rest of the
// compiler is going to lean on.
//
// true, false and nil have an opcode each and skip the pool entirely. there are
// only three of them, they never change, and spending a constant slot plus an
// index byte on `true` when a single opcode does the job would be silly.
//
// everything else goes through the pool. the instruction stream is a byte
// vector, and a 64-bit int or a string has no way of fitting into the one byte
// an operand gets, so the value is stored off to the side once and Const carries
// the index to find it again.
void Compiler::visit_literal(LiteralExpr& expr) {
    set_line(expr.token);

    switch (expr.token.kind) {
    case TokenKind::True:
        emit(Opcode::True);
        break;
    case TokenKind::False:
        emit(Opcode::False);
        break;
    case TokenKind::Nil:
        emit(Opcode::Nil);
        break;
    default:
        emit_constant(literal_value(expr.token), expr.token);
        break;
    }
}

// `a + b` becomes: whatever pushes a, whatever pushes b, then one Add. the
// operator instruction carries no operands at all — it pops the two values the
// operands left behind and pushes the result, so the same Add works no matter how
// big the subtrees under it were.
//
// the child order matters and is not just convention. the stack hands things back
// in reverse, so the VM pops the right operand first and the left one second;
// compiling them the other way round would still run for `a + b` but would give
// `b - a` for a subtraction, and finding that later from a wrong answer is much
// worse than getting it right here.
//
// nesting takes care of itself. `1 + 2 * 3` parsed with the right precedence has
// the multiply as the right child, so recursing into it emits Const 2, Const 3,
// Mul before the Add ever runs, and the Add sees a single value sitting there —
// exactly what it would see from a plain literal.
//
// comparisons ride along the same path, since the shape is identical: two
// operands on the stack, one instruction that pops both and pushes one value
// back. the only difference is that <= and >= leave a Not behind them, and that
// still ends with one value on the stack, so the rule holds either way.
void Compiler::visit_binary(BinaryExpr& expr) {
    Opcode op;
    bool negate = false;
    if (!arithmetic_opcode(expr.op.kind, op) && !comparison_opcode(expr.op.kind, op, negate)) {
        fail("operator '" + expr.op.lexeme + "' cannot be compiled yet", expr.op);
    }

    compile_expr(*expr.left);
    compile_expr(*expr.right);

    // both children stamped their own lines on the way through, so line_ is
    // wherever the right operand ended. put it back before emitting, or an error
    // on the Add points at the operand instead of the operator.
    set_line(expr.op);
    emit(op);

    // the Not belongs to the same operator, so it gets the same line — a `<=`
    // split across two lines shouldn't have its two instructions blaming
    // different ones.
    if (negate) {
        emit(Opcode::Not);
    }
}

// `-x` and `!done` are the binary case with one child instead of two: compile the
// operand, then one instruction that pops it and pushes the result. so the same
// one-value-on-the-stack rule holds here too, and a unary can sit anywhere an
// operand can without the code around it caring.
//
// Not is the same instruction `<=` and `>=` already emit after their compare, so
// `!` needs nothing new — both uses want the shared truthiness rule and both give
// back a bool.
//
// the parser nests these rather than counting them, so `--7` is a UnaryExpr
// holding another one, and recursing gives Const 7, Negate, Negate. two negates
// that cancel out is a wasted instruction, but folding them away is the peephole
// pass in Phase 6, not something to guess at here.
void Compiler::visit_unary(UnaryExpr& expr) {
    Opcode op;
    switch (expr.op.kind) {
    case TokenKind::Minus:
        op = Opcode::Negate;
        break;
    case TokenKind::Bang:
        op = Opcode::Not;
        break;
    default:
        // only Minus and Bang ever build a UnaryExpr, so getting here means a new
        // prefix operator was added to the parser and not to this switch.
        fail("operator '" + expr.op.lexeme + "' cannot be used as a prefix operator", expr.op);
    }

    compile_expr(*expr.operand);

    // the operand left line_ wherever it ended, same as in visit_binary. put the
    // operator's line back before emitting, or negating something that spans two
    // lines blames the wrong one.
    set_line(expr.op);
    emit(op);
}

// reading a variable back out. the name goes in the constant pool and GetGlobal
// carries its index, so the VM does the lookup at run time instead of the
// compiler resolving it now.
//
// that sounds backwards for something called a compiler, but a global can be
// defined after the code that reads it was already compiled — a function body
// mentioning `print` is compiled before anything has bound `print` — and a name
// is the only handle both halves can agree on before the program runs.
//
// a local is the case where the compiler does know the answer up front, so it is
// tried first: the name was declared in a block we are still inside, which means
// its slot number is already sitting in locals_ and GetLocal can carry it
// directly. no pool entry, no hash lookup at run time, just an index into the
// stack.
//
// the order between the two is not a preference, it is the scoping rule. a local
// has to win over a global of the same name, or `let x = 1` inside a block would
// still read whatever the file bound `x` to at the top.
void Compiler::visit_identifier(IdentifierExpr& expr) {
    set_line(expr.token);

    int slot = resolve_local(expr.name);
    if (slot >= 0) {
        // the cast is safe because add_local refuses the 257th local, so no slot
        // that made it into locals_ is past 255.
        emit(Opcode::GetLocal, static_cast<uint8_t>(slot));
        return;
    }

    emit(Opcode::GetGlobal, name_constant(expr.name, expr.token));
}

// `let x = expr` compiles the initializer first, so the value it wants to bind is
// sitting on top when DefineGlobal runs, and then names it. the order is forced:
// the instruction takes the value off the stack, so nothing can be bound before
// the code that produces it has run.
//
// DefineGlobal pops. a statement has to leave the stack the way it found it, and
// the initializer put exactly one value there, so exactly one comes off.
//
// a `let` with nothing after the name binds nil. the grammar does not allow that
// today, but the tree-walker's visit_let already checks for it and the two
// backends are supposed to behave the same, so the check is here too rather than
// waiting for the grammar to catch up.
void Compiler::visit_let(LetStmt& stmt) {
    if (stmt.initializer) {
        compile_expr(*stmt.initializer);
    } else {
        set_line(stmt.name_token);
        emit(Opcode::Nil);
    }

    // the initializer left line_ wherever its last node was, same trap as in
    // visit_binary. put the name's line back so an error on the bind points at
    // the `let` and not at the tail of a long expression.
    set_line(stmt.name_token);

    // inside a block there is no instruction at all. the initializer already put
    // the value on the stack and that spot is the variable — the compiler just
    // writes down which slot it landed in and every later read of the name turns
    // into that number. so a local costs nothing to declare, where a global costs
    // a pool entry, an instruction and a hash lookup on every use.
    //
    // it also means the local is only declared *after* its initializer compiled,
    // which is what makes `{ let x = x }` read the outer x rather than itself.
    // the tree-walker gets the same answer for the same reason: it calls define()
    // with the value once it has one.
    if (scope_depth_ > 0) {
        add_local(stmt.name, stmt.name_token);
        return;
    }

    emit(Opcode::DefineGlobal, name_constant(stmt.name, stmt.name_token));
}

// an expression on a line of its own, like `print(x)`. compile it the normal way
// and then throw the value away again, because a statement has to leave the stack
// exactly as it found it.
//
// that rule is worth more than it looks. a while loop jumps back to its condition
// on every turn, and if each pass through the body left one value behind the stack
// would grow with the iteration count instead of staying flat. locals in step 76
// are named by a slot number the compiler works out by counting what is on the
// stack, and that count is only right while every statement balances. so this one
// Pop is what the rest of the phase is built on top of.
//
// there is no set_line here, unlike everywhere else. the other visitors put their
// own token's line back because their instruction belongs to an operator or a
// name; this Pop belongs to the end of the expression, which is where line_ is
// already sitting. ExprStmt has no token of its own to ask anyway.
void Compiler::visit_expr_stmt(ExprStmt& stmt) {
    compile_expr(*stmt.expr);
    emit(Opcode::Pop);
}

// `{ ... }` is its own scope, so anything `let` binds inside is gone again on the
// way out. the statements themselves need nothing special — they compile exactly
// as they would at the top level, the depth is what changes where their `let`s
// end up.
//
// the Pops end_scope emits are the whole reason a block is more than a loop over
// its statements. the values are on the stack and only the compiler knows how
// many of them there are, since it is the one that counted the slots.
//
// there is no set_line, same as visit_expr_stmt: a block has no token of its own
// and line_ is already at the last statement in it, which is close enough to the
// closing brace for the Pops to blame.
void Compiler::visit_block(BlockStmt& stmt) {
    begin_scope();

    for (std::unique_ptr<Stmt>& inner : stmt.statements) {
        compile_stmt(*inner);
    }

    end_scope();
}

// everything below is a stub until the step that fills it in. the parameters are
// cast to void so -Wunused-parameter stays quiet without the names disappearing
// from the signatures, which would make the diffs in those steps harder to read.

void Compiler::visit_call(CallExpr& expr) { (void)expr; }

void Compiler::visit_assign(AssignExpr& expr) { (void)expr; }

void Compiler::visit_if(IfStmt& stmt) { (void)stmt; }

void Compiler::visit_while(WhileStmt& stmt) { (void)stmt; }

void Compiler::visit_fn(FnDecl& stmt) { (void)stmt; }

void Compiler::visit_return(ReturnStmt& stmt) { (void)stmt; }

} // namespace brewc
