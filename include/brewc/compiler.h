#ifndef BREWC_COMPILER_H
#define BREWC_COMPILER_H

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "brewc/ast.h"
#include "brewc/chunk.h"
#include "brewc/token.h"

namespace brewc {

// thrown when a program parses fine but can't be turned into bytecode. the two
// cases coming up are a chunk with more than 256 constants in it and a function
// with more locals than a one-byte slot number can name — both are limits of the
// encoding rather than anything wrong with the source, but the user still has to
// be told which line ran into them. same shape as ParseError, and the location is
// already baked into what(), so printing it is enough.
class CompileError : public std::runtime_error {
public:
    CompileError(int line, int column, const std::string& message);

    int line() const { return line_; }
    int column() const { return column_; }

private:
    int line_;
    int column_;
};

// walks the AST once and writes out bytecode for it, which the VM then runs
// without ever looking at the tree again. that's the whole reason this exists:
// the tree-walker re-visits the same nodes on every loop iteration and pays for
// the virtual dispatch each time, and none of that work changes between runs.
//
// it's a visitor over both halves of the tree like the interpreter is, and for
// the same reason — expressions and statements are separate hierarchies. the
// difference is what the visit_* bodies do. the interpreter computes a value and
// hands it back through result_; these just append bytes to chunk_ and return
// nothing, because the value doesn't exist yet at compile time. an expression
// compiles to instructions that leave its result on the VM stack, so the
// convention every visit_* below follows is: whatever you compile, exactly one
// value is on the stack afterwards.
//
// literals, arithmetic and comparisons are wired up so far. the rest of the
// visit_* bodies are still stubs and get filled in one at a time over the next
// steps.
class Compiler : public Visitor, public StmtVisitor {
public:
    Compiler();

    // compile a whole program into one chunk and hand it over. the chunk always
    // ends in a Return so the VM has a defined place to stop, even for an empty
    // file. calling this again starts from a clean chunk rather than appending to
    // the last one.
    Chunk compile(std::vector<std::unique_ptr<Stmt>>& program);

    // same thing for a single expression. the REPL and most of the tests only
    // care about one expression at a time, and going through a whole program just
    // to check what `1 + 2` compiles to is a lot of ceremony for nothing.
    Chunk compile_expression(Expr& expr);

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

private:
    // compile one node. thin wrappers around accept() so the visit_* bodies can
    // recurse into their children by name instead of spelling out the dispatch.
    void compile_expr(Expr& expr);
    void compile_stmt(Stmt& stmt);

    // start a fresh chunk. compile() and compile_expression() both call this
    // first, so neither one can be handed leftovers from an earlier run.
    void reset();

    // append an instruction at the line we're currently compiling. every emit
    // goes through here, which is why line_ exists — passing a line down through
    // every visit_* just to reach this point would mean threading a parameter
    // through the whole visitor interface.
    void emit(Opcode op);

    // an instruction plus its one-byte operand, written as a pair because they
    // always travel together and forgetting the second half leaves a chunk the
    // disassembler reports as truncated.
    void emit(Opcode op, uint8_t operand);

    // a bare byte, for the operands that get patched in later — a forward jump
    // writes placeholder bytes now and fills them in once it knows how far it has
    // to reach.
    void emit_byte(uint8_t byte);

    // put a value in the chunk's pool and emit the Const that pushes it. `where`
    // is only used if the pool is already full, in which case the compile stops
    // and points at that token. the check lives here rather than at the call
    // sites because add_constant reports a full pool by handing back an index
    // that doesn't exist, and emitting that byte anyway would have the VM push
    // whatever constant_at decides to give it.
    void emit_constant(Value value, const Token& where);

    // put a variable's name in the pool and hand back the index, without emitting
    // anything. globals are addressed by name rather than by slot, so the three
    // global opcodes all need this index as their operand — but each of them
    // wants it at a different point relative to the value being bound, so the
    // Const half of emit_constant would be in the way.
    uint8_t name_constant(const std::string& name, const Token& where);

    // give up on the whole compile with a CompileError pointed at `where`. every
    // failure funnels through here so the location handling stays in one spot,
    // the same way the parser does it. always throws.
    [[noreturn]] void fail(const std::string& message, const Token& where);

    // remember which source line the node being compiled came from, so the
    // instructions it emits get stamped with it. a visit_* sets this from its own
    // token before emitting anything.
    void set_line(const Token& token);

    Chunk chunk_;
    // the line every emit right now gets recorded under. starts at 1 so a chunk
    // that somehow emits before any node is visited still has a sane line rather
    // than a zero the error reporter would have to special-case.
    int line_ = 1;
};

} // namespace brewc

#endif
