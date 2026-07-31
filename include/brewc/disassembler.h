#ifndef BREWC_DISASSEMBLER_H
#define BREWC_DISASSEMBLER_H

#include <cstddef>
#include <string>

#include "brewc/chunk.h"

namespace brewc {

// turns a chunk back into something readable. the compiler in the next steps
// writes bytes, and staring at a vector of numbers to work out whether it wrote
// the right ones is hopeless, so everything from here on gets checked by dumping
// the chunk and reading it.
//
// the output is one line per instruction, roughly:
//
//     == main ==
//     0000    1 Const            0 '42'
//     0002    | Negate
//     0003    2 JumpIfFalse      3 -> 9
//
// first column is the byte offset the instruction starts at, which is also what
// jump offsets are counted from. second is the source line, or a bar when it's
// the same line as the instruction above so a long statement doesn't repeat
// itself. then the opcode and whatever operands it carries.

// disassemble the one instruction that starts at offset, append its line to out,
// and hand back the offset of the next one. the caller can't just add one to walk
// the stream — instructions are different lengths, and only the opcode says how
// long this one is.
//
// a stream that ends in the middle of an instruction still returns something past
// the end so a loop over this terminates rather than reading the same broken byte
// forever.
std::size_t disassemble_instruction(const Chunk& chunk, std::size_t offset, std::string& out);

// the whole chunk, with a header naming it. name is just a label for the dump —
// "main" for the top level, the function's name for a fn body once step 75 gives
// each one its own chunk.
std::string disassemble(const Chunk& chunk, const std::string& name);

} // namespace brewc

#endif
