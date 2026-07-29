#ifndef BREWC_CHUNK_H
#define BREWC_CHUNK_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
    // push a value from the chunk's constant pool. the pool itself lands in the
    // next step, but the opcode is listed now so the enum is complete.
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
struct Chunk {
    std::vector<uint8_t> code;
    std::vector<int> lines;

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
};

} // namespace brewc

#endif
