#ifndef BREWC_CHUNK_H
#define BREWC_CHUNK_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "brewc/value.h"

namespace brewc {

// one instruction the VM knows how to run. the tree-walker was handed the AST and
// walked it every time; from here on the compiler flattens that tree into a list
// of these once, and the VM just marches through the list.
//
// the underlying type is fixed at uint8_t on purpose. instructions go into a plain
// byte buffer, so an opcode has to fit in exactly one byte — which also caps us at
// 256 of them, plenty for this language.
//
// some of these carry operands in the bytes that follow (Const takes an index,
// the jumps take an offset); the rest are bare. nothing here decodes them yet,
// that's the disassembler's job in the next couple of steps.
enum class Opcode : uint8_t {
    // push a value from the chunk's constant pool. the byte after it is the index
    // into that pool.
    Const,

    // the three values that don't need a pool entry — pushing a dedicated
    // instruction is cheaper than burning a constant slot on `true`.
    Nil,
    True,
    False,

    // arithmetic. all of these pop their operands off the stack and push the
    // result back, so the instruction itself needs no operands.
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Negate,

    // comparison and logical. Less/Greater have no <= or >= twins here: the
    // compiler can build those out of a compare plus a Not.
    Equal,
    NotEqual,
    Less,
    Greater,
    Not,

    // globals are looked up by name, so all three of these take a one-byte index
    // into the constant pool where the name is kept as a string. that is a hash
    // lookup every time the variable is touched, which is slower than a slot
    // would be, but a global can be defined after the code that reads it was
    // already compiled and a name is the only thing both sides agree on.
    //
    // DefineGlobal pops the value it binds, since `let` is a statement and has to
    // leave the stack the way it found it. SetGlobal leaves it, since assignment
    // is an expression and `a = b = 7` needs the value to still be there.
    DefineGlobal,
    GetGlobal,
    SetGlobal,

    // locals live in a slot on the VM stack, and both of these take that slot
    // number as a one-byte operand.
    GetLocal,
    SetLocal,

    // throw away the value on top. statements leave their result behind and
    // nobody wants it.
    Pop,

    // control flow. Jump and JumpIfFalse skip forward, Loop jumps backward for
    // while loops. all three carry a two-byte offset.
    Jump,
    JumpIfFalse,
    Loop,

    // Call takes the argument count as an operand; Return hands the value on top
    // back to whoever called us.
    Call,
    Return,

    // closures. Closure takes a pool index for the CompiledFn, then two bytes per
    // upvalue (is_local, index). Get/SetUpvalue take the upvalue's index in the
    // running closure. CloseUpvalue is a Pop for a local some closure captured,
    // it moves the value off the stack before dropping the slot.
    Closure,
    GetUpvalue,
    SetUpvalue,
    CloseUpvalue,
};

// the name of an opcode as written in the enum, for the disassembler and for test
// failures that are otherwise just numbers.
std::string opcode_name(Opcode op);

// a compiled hunk of bytecode — one per function body, plus one for the top level
// of the program.
//
// code is the instruction stream: opcodes and their operands packed together, no
// separation between them, which is why decoding has to start at a known
// instruction boundary and walk forward.
//
// lines is one source line per byte of code, not per instruction. keeping it a
// flat parallel array wastes a bit of memory on operand bytes, but it means a
// runtime error only has to index by the same offset it already has in hand. the
// alternative is compressing runs of equal lines, and that's not worth it while
// the language is this small.
//
// constants is the pool. a literal like 3.14 or "hello" can't be packed into the
// instruction stream — the stream is bytes, and a double is eight of them — so the
// value goes in here once and the instruction only carries its index. that also
// means a literal used ten times costs one slot instead of ten.
struct Chunk {
    // Const's operand is a single byte, so index 255 is the last one we can name.
    // add_constant reports a full pool by handing back this number, which is one
    // past the largest valid index.
    static constexpr std::size_t max_constants = 256;

    std::vector<uint8_t> code;
    std::vector<int> lines;
    std::vector<Value> constants;

    // append a raw byte. used for operands, and by the overload below for the
    // opcodes themselves.
    void write(uint8_t byte, int line);

    // same thing for an opcode, so callers don't have to cast at every call site.
    void write(Opcode op, int line);

    // how many bytes are in the stream. also the offset the next write will land
    // at, which is what the jump patching in step 73 needs.
    std::size_t size() const;

    // the source line the byte at this offset came from. out of range gives back
    // 0 rather than reading past the end — a bad line number in an error message
    // is annoying, a crash while reporting an error is worse.
    int line_at(std::size_t offset) const;

    // put a value in the pool and hand back the index to write after a Const. an
    // equal number or string already in there is reused instead of stored twice.
    //
    // if the pool is full the value is dropped and max_constants comes back, so
    // the caller has to check before emitting the index. Chunk has no way to
    // report an error itself — the compiler is the one holding the source location
    // to complain about.
    std::size_t add_constant(Value value);

    // the value at this index. an index the pool doesn't have gives back nil, same
    // reasoning as line_at.
    const Value& constant_at(std::size_t index) const;
};

// what `fn name(a, b) { ... }` compiles to. the body gets a chunk of its own
// instead of being spliced into the one around it, so the outer code doesn't run
// it on the way past and a call (step 82) has something to point ip at.
//
// arity is checked against the argument count at the call. upvalue_count is how
// many outer variables the body uses, and so how many (is_local, index) pairs
// come after the Closure instruction that makes it.
struct CompiledFn {
    Chunk chunk;
    int arity = 0;
    std::string name;
    int upvalue_count = 0;
};

// one variable a closure captured. while the variable's slot is still on the
// stack the upvalue is open and just remembers the slot, so writes through the
// closure and writes through the local hit the same value. once the slot goes
// away (end of block or the fn returns) it gets closed: the value is copied into
// `closed` and from then on that copy is the variable.
//
// plan.md has a Value* into the stack here. the stack is a std::vector though,
// and a push that makes it reallocate would leave every open upvalue pointing at
// freed memory, so it's a slot number instead.
struct Upvalue {
    std::size_t slot = 0;
    bool open = true;
    Value closed;
};

// a fn plus the variables it captured. two closures made from the same outer
// variable hold the same Upvalue (shared_ptr), that's how a change made by one
// is seen by the other.
//
// a fn that captures nothing is never wrapped in one of these, the compiler
// just pushes the CompiledFn with a Const like before.
struct Closure {
    std::shared_ptr<CompiledFn> fn;
    std::vector<std::shared_ptr<Upvalue>> upvalues;
};

} // namespace brewc

#endif
