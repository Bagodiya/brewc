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

// everything below is a stub until the step that fills it in. the parameters are
// cast to void so -Wunused-parameter stays quiet without the names disappearing
// from the signatures, which would make the diffs in those steps harder to read.

void Compiler::visit_identifier(IdentifierExpr& expr) { (void)expr; }

void Compiler::visit_binary(BinaryExpr& expr) { (void)expr; }

void Compiler::visit_unary(UnaryExpr& expr) { (void)expr; }

void Compiler::visit_call(CallExpr& expr) { (void)expr; }

void Compiler::visit_let(LetStmt& stmt) { (void)stmt; }

void Compiler::visit_if(IfStmt& stmt) { (void)stmt; }

void Compiler::visit_while(WhileStmt& stmt) { (void)stmt; }

void Compiler::visit_block(BlockStmt& stmt) { (void)stmt; }

void Compiler::visit_fn(FnDecl& stmt) { (void)stmt; }

} // namespace brewc
