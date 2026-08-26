#ifndef BREWC_VM_H
#define BREWC_VM_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "brewc/chunk.h"
#include "brewc/runtime_error.h"
#include "brewc/value.h"

namespace brewc {

// how a run ended. the VM either walked the chunk to its Return or it hit
// something it could not do. the message and the line that go with the second
// case hang off the VM and are read back with error(), so this stays a plain
// yes/no answer — the same way the parser keeps its errors in a vector instead of
// stuffing them into the return type.
enum class InterpretResult {
    Ok,
    RuntimeError,
};

// runs a compiled chunk. the tree-walker asks each AST node what it is and then
// dispatches on the answer; this reads a byte, jumps on it, and moves to the next
// one, which is the whole point of having compiled the tree in the first place.
//
// operands live on a stack rather than in registers. an instruction like Add
// takes nothing with it in the stream: the two values it wants are already on top
// because the instructions that pushed them ran first. that's why the compiler
// emits left before right, and why every visit_* in it leaves exactly one value
// behind — the two halves only fit together if both sides keep that promise.
//
// Const, Return, the arithmetic opcodes and the comparisons are implemented here.
// the rest stop the run with a RuntimeError instead of falling through to
// something worse, and get filled in one group at a time over the next steps.
class VM {
public:
    VM();

    // execute a chunk from its first byte and stop at the Return that ends it.
    // the stack is cleared first, so the same VM can run a second chunk without
    // seeing what the last one left behind. the chunk is only read, never
    // written, which is what lets the same compiled chunk run more than once.
    InterpretResult run(const Chunk& chunk);

    // what is on top of the stack, for the tests. a run normally ends with its
    // result sitting there, and reaching in is a lot less work than adding a way
    // to print it just to check a number came out right.
    //
    // an empty stack gives back nil rather than reading off the end. a test that
    // expected a value and got nil fails on the comparison, which is a better
    // failure than the undefined behaviour the alternative has.
    const Value& stack_top() const;

    // how many values are left. the checks that matter are usually "one value,
    // and it is this one" — a run that pushed twice and popped once is wrong even
    // when the top of the stack looks right.
    std::size_t stack_size() const;

    // what went wrong in the last run, or null if it went fine. the caller prints
    // it with format_error() the same way it prints one out of the tree-walker —
    // a user has no idea which backend ran their program and should not be able
    // to tell from the error.
    //
    // this is only meaningful right after run() returns. a second run clears it
    // before it starts, so a failed run followed by a good one reads as good.
    const RuntimeError* error() const;

private:
    // stop the run, remember why, and say so. the offending instruction is the
    // byte before ip_, since ip_ has already stepped past whatever was read, and
    // the chunk's parallel lines array turns that offset into a source line.
    //
    // hands back RuntimeError so the dispatch loop can `return fail(...)` on one
    // line instead of setting the error and then returning the same thing at
    // every site.
    InterpretResult fail(const std::string& message, const Chunk& chunk);

    // the next byte in the stream, stepping ip past it. operands are read the
    // same way the opcodes are, since the two are packed together with nothing
    // marking where one ends.
    uint8_t read_byte(const Chunk& chunk);

    void push(Value value);

    // take the top value off. the arithmetic opcodes pop their operands and push
    // the answer back, which is what keeps the stack one value deeper per
    // expression however long the expression is.
    Value pop();

    // look at a value without removing it. distance 0 is the top, 1 the one under
    // it. nothing needs it yet — the arithmetic opcodes pop both operands and let
    // the value_ops helpers decide whether the pair made sense — but the jumps in
    // step 78 have to read a condition they are not allowed to consume.
    const Value& peek(std::size_t distance) const;

    std::vector<Value> stack_;

    // where the next instruction starts, as an offset into chunk.code. an index
    // and not a pointer on purpose: a pointer into the vector's buffer is only
    // good until something makes that vector reallocate, and an offset survives
    // it. nothing reallocates a chunk mid-run today, but the jumps in step 78
    // work in offsets anyway, so a pointer would have to be converted back at
    // every one of them.
    std::size_t ip_ = 0;

    // set by fail(), cleared at the top of every run. empty is the normal state,
    // which is why error() can answer with a pointer instead of the caller having
    // to ask whether there is one first.
    std::optional<RuntimeError> error_;
};

} // namespace brewc

#endif
