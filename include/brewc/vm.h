#ifndef BREWC_VM_H
#define BREWC_VM_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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

// one call that is currently running. the frames sit in a stack of their own,
// next to the value stack rather than mixed into it, which is what keeps a
// return from having to search for where the caller left off.
//
// fn is held by shared_ptr and not by a raw Chunk pointer, because the chunk the
// VM is reading instructions out of lives inside it. the same function is also
// sitting on the value stack underneath its arguments, but a body is free to
// write over that slot, so the frame keeping its own reference is what stops the
// code being executed from being freed halfway through executing it.
//
// return_ip is the caller's ip, not this frame's. the VM only ever runs one
// chunk at a time and ip_ already tracks where it is in that one, so the thing
// worth writing down is where to carry on once this call is finished.
//
// slot_base is where the callee ended up on the value stack. every local in the
// body is counted from there, so the same GetLocal 1 means a different value in
// each frame and a function can call itself without anything being renamed.
//
// call_line is the source line the call was written on, kept for the stack trace
// and nothing else — the line inside the body is already in the chunk.
struct CallFrame {
    std::shared_ptr<CompiledFn> fn;
    std::size_t return_ip = 0;
    std::size_t slot_base = 0;
    int call_line = 0;
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
// every opcode the compiler emits has a case in the dispatch loop now. Return is
// the one that is only half done: it stops the run instead of handing a value
// back to the caller, so a call reaches the body and the body's Return ends the
// whole program. step 83 unwinds the frame properly.
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

    // how many calls are in flight. the tests want this to tell a call that
    // pushed a frame from one that was refused before it got that far, since
    // both leave the stack looking much the same.
    std::size_t frame_depth() const;

    // what a global is bound to, or null if that name was never defined. for the
    // tests, same as stack_top — a `let` leaves nothing on the stack, so without
    // this there is no way to tell a DefineGlobal that worked from one that
    // quietly did nothing.
    const Value* global(const std::string& name) const;

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

    // the next two bytes as one number, high byte first. only the jumps carry an
    // operand this wide — one byte would cap a branch at 255 bytes of code, which
    // is a couple of dozen statements and nowhere near enough.
    uint16_t read_short(const Chunk& chunk);

    void push(Value value);

    // take the top value off. the arithmetic opcodes pop their operands and push
    // the answer back, which is what keeps the stack one value deeper per
    // expression however long the expression is.
    Value pop();

    // look at a value without removing it. distance 0 is the top, 1 the one under
    // it. the two store opcodes are what want this: an assignment has to leave
    // its value on the stack for whatever is around it, so it reads the top
    // without consuming it.
    const Value& peek(std::size_t distance) const;

    // where the running frame's slot 0 sits on the value stack. the top level is
    // not a frame and its locals start at 0, so an empty frame stack answers 0
    // and GetLocal keeps meaning what it did before calls existed.
    std::size_t frame_base() const;

    // the frames as a trace, outermost call first — the order the interpreter
    // builds call_stack_ in, since format_error walks it backwards to print the
    // innermost one at the top.
    std::vector<TraceFrame> call_trace() const;

    // how deep the calls are allowed to get. a runaway recursion has to be
    // caught by something, and a count is the only thing there is to catch it
    // with: the frames live in a vector on the heap, so the C++ stack never runs
    // out and the process would just grow until the allocator gave up.
    //
    // 256 is a guess in the same spirit as the stack reserve below — deeper than
    // any sensible program and shallow enough that a mistake is reported in no
    // time. the tree-walker has no equivalent limit, which is why a recursion
    // that never ends takes the whole process down there and stops cleanly here.
    static constexpr std::size_t max_frames = 256;

    std::vector<Value> stack_;

    // one entry per call that has not returned yet, the caller underneath the
    // callee. empty means the top level, which is deliberately not a frame of its
    // own: run() is handed a plain Chunk with no CompiledFn wrapped round it, so
    // there would be nothing to put in one.
    //
    // cleared by run() the same way the stack is. a run that stopped mid-call
    // left its frames behind, and starting the next chunk inside somebody else's
    // frame would read locals out of slots that hold nothing.
    std::vector<CallFrame> frames_;

    // every global the program has defined, by name. deliberately not cleared by
    // run(), unlike the stack: the repl keeps one VM for the whole session and
    // runs each line as its own chunk, so wiping this would mean a `let` on one
    // line was gone by the next. the tree-walker's Interpreter holds on to its
    // global Environment for exactly the same reason.
    //
    // a map and not a vector of slots because the compiler cannot number these.
    // it does not know how many globals a program ends up with until it has
    // compiled all of it, and a function body can name one that is defined
    // further down the file, so the lookup has to happen at run time off the
    // name.
    std::unordered_map<std::string, Value> globals_;

    // where the next instruction starts, as an offset into chunk.code. an index
    // and not a pointer on purpose: a pointer into the vector's buffer is only
    // good until something makes that vector reallocate, and an offset survives
    // it. nothing reallocates a chunk mid-run today, but the jumps work in
    // offsets anyway, so a pointer would have to be converted back at every one
    // of them.
    std::size_t ip_ = 0;

    // set by fail(), cleared at the top of every run. empty is the normal state,
    // which is why error() can answer with a pointer instead of the caller having
    // to ask whether there is one first.
    std::optional<RuntimeError> error_;
};

} // namespace brewc

#endif
