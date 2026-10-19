#include "brewc/disassembler.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace brewc {

namespace {

// widths for the columns. the opcode column is wide enough for DefineGlobal, the
// longest name in the enum, plus a space before the operands.
constexpr int offset_width = 4;
constexpr int line_width = 4;
constexpr int name_width = 16;

// the offset and the source line, which every line of the dump starts with.
// a byte with the same line as the one before it prints a bar instead of the
// number — `1 + 2 * 3` is one line and half a dozen instructions, and repeating
// the 1 six times makes the column harder to read, not easier.
void write_prefix(const Chunk& chunk, std::size_t offset, std::ostringstream& out) {
    out << std::setfill('0') << std::setw(offset_width) << offset << std::setfill(' ');

    bool same_line = offset > 0 && chunk.line_at(offset) == chunk.line_at(offset - 1);
    if (same_line) {
        out << std::setw(line_width) << "|";
    } else {
        out << std::setw(line_width) << chunk.line_at(offset);
    }
    out << ' ';
}

void write_name(Opcode op, std::ostringstream& out) {
    out << std::left << std::setw(name_width) << opcode_name(op) << std::right;
}

// an instruction with no operands. the name on its own is the whole line.
std::size_t simple(const Chunk& chunk, Opcode op, std::size_t offset, std::ostringstream& out) {
    write_prefix(chunk, offset, out);
    // no padding here, nothing follows the name and trailing spaces would only
    // show up as noise in the test comparisons.
    out << opcode_name(op);
    return offset + 1;
}

// Const and the three global opcodes. all four carry an index into the constant
// pool, and the index alone is not much use when you're trying to see whether the
// right literal got stored or which name a global reads, so the value it points
// at comes along in quotes. for a global that value is the name itself, which is
// the whole point of dumping it — `GetGlobal 2` tells you nothing, `GetGlobal 2
// 'count'` tells you what the instruction does.
std::size_t constant(const Chunk& chunk, Opcode op, std::size_t offset, std::ostringstream& out) {
    uint8_t index = chunk.code[offset + 1];
    write_prefix(chunk, offset, out);
    write_name(op, out);
    out << std::setw(4) << static_cast<int>(index) << " '"
        << to_string(chunk.constant_at(index)) << "'";
    return offset + 2;
}

// GetLocal, SetLocal, Call and the two upvalue ones. all take one byte that
// means a number — a stack slot, an argument count, an upvalue index — and
// there's nothing to look up, so the byte is printed on its own.
std::size_t byte_operand(const Chunk& chunk, Opcode op, std::size_t offset,
                         std::ostringstream& out) {
    write_prefix(chunk, offset, out);
    write_name(op, out);
    out << std::setw(4) << static_cast<int>(chunk.code[offset + 1]);
    return offset + 2;
}

// the jumps. their operand is two bytes, high byte first, holding how far to move
// rather than where to land — the compiler patches these in without knowing the
// absolute address, and a relative offset is the same number no matter where the
// chunk ends up.
//
// sign says which way to count: forward for Jump and JumpIfFalse, backward for
// Loop. either way the target is printed too, since working out 9 from "3" and
// the offset of the next instruction is exactly the arithmetic this is meant to
// save you.
std::size_t jump(const Chunk& chunk, Opcode op, std::size_t offset, int sign,
                 std::ostringstream& out) {
    int distance = (static_cast<int>(chunk.code[offset + 1]) << 8) |
                   static_cast<int>(chunk.code[offset + 2]);
    std::size_t next = offset + 3;

    write_prefix(chunk, offset, out);
    write_name(op, out);
    out << std::setw(4) << distance << " -> "
        << static_cast<long long>(next) + sign * distance;
    return next;
}

// how many upvalue pairs follow a Closure. that's not in the instruction, it's
// in the fn the operand points at, so a bad operand just means no pairs.
std::size_t closure_pairs(const Chunk& chunk, std::size_t offset) {
    const Value& fn = chunk.constant_at(chunk.code[offset + 1]);
    if (!is_compiled_fn(fn)) return 0;
    return static_cast<std::size_t>(std::get<std::shared_ptr<CompiledFn>>(fn)->upvalue_count);
}

// Closure prints like a Const and then one extra line per captured variable,
// saying whether it comes from a local slot of the enclosing fn or from one of
// its upvalues.
std::size_t closure(const Chunk& chunk, std::size_t offset, std::ostringstream& out) {
    std::size_t next = constant(chunk, Opcode::Closure, offset, out);

    std::size_t pairs = closure_pairs(chunk, offset);
    for (std::size_t i = 0; i < pairs; ++i) {
        bool is_local = chunk.code[next] != 0;
        int index = chunk.code[next + 1];
        out << "\n";
        write_prefix(chunk, next, out);
        out << (is_local ? "  local " : "  upvalue ") << index;
        next += 2;
    }
    return next;
}

// how many bytes an opcode takes up in total, itself included. used to check the
// stream has them before reading, so a chunk that was cut short mid-instruction
// gets a complaint instead of an out of bounds read.
std::size_t instruction_length(Opcode op) {
    switch (op) {
    case Opcode::Const:
    case Opcode::DefineGlobal:
    case Opcode::GetGlobal:
    case Opcode::SetGlobal:
    case Opcode::GetLocal:
    case Opcode::SetLocal:
    case Opcode::Call:
    case Opcode::GetUpvalue:
    case Opcode::SetUpvalue:
    case Opcode::Closure:
        return 2;
    case Opcode::Jump:
    case Opcode::JumpIfFalse:
    case Opcode::Loop:
        return 3;
    default:
        return 1;
    }
}

// true when the byte at this offset is an opcode we know. anything else is a
// compiler bug, and the dump is what's supposed to help find it, so it has to
// survive being handed nonsense.
bool known_opcode(uint8_t byte) {
    return opcode_name(static_cast<Opcode>(byte)) != "<unknown>";
}

} // namespace

std::size_t disassemble_instruction(const Chunk& chunk, std::size_t offset, std::string& out) {
    std::ostringstream line;

    if (offset >= chunk.size()) {
        // shouldn't happen from disassemble() below, but a caller stepping through
        // by hand can walk off the end.
        out += "<past end of chunk>";
        return offset + 1;
    }

    uint8_t byte = chunk.code[offset];
    if (!known_opcode(byte)) {
        write_prefix(chunk, offset, line);
        line << "<unknown " << static_cast<int>(byte) << ">";
        out += line.str();
        return offset + 1;
    }

    Opcode op = static_cast<Opcode>(byte);
    // a Closure's pairs are part of it too, but only once the index byte is
    // there to say how many
    std::size_t length = instruction_length(op);
    if (op == Opcode::Closure && offset + length <= chunk.size()) {
        length += 2 * closure_pairs(chunk, offset);
    }

    if (offset + length > chunk.size()) {
        write_prefix(chunk, offset, line);
        line << opcode_name(op) << " <truncated>";
        out += line.str();
        // there is no next instruction to point at, so land past the end and let
        // the loop stop.
        return chunk.size();
    }

    std::size_t next = offset;
    switch (op) {
    case Opcode::Const:
    case Opcode::DefineGlobal:
    case Opcode::GetGlobal:
    case Opcode::SetGlobal:
        next = constant(chunk, op, offset, line);
        break;
    case Opcode::GetLocal:
    case Opcode::SetLocal:
    case Opcode::Call:
    case Opcode::GetUpvalue:
    case Opcode::SetUpvalue:
        next = byte_operand(chunk, op, offset, line);
        break;
    case Opcode::Closure:
        next = closure(chunk, offset, line);
        break;
    case Opcode::Jump:
    case Opcode::JumpIfFalse:
        next = jump(chunk, op, offset, 1, line);
        break;
    case Opcode::Loop:
        next = jump(chunk, op, offset, -1, line);
        break;
    default:
        next = simple(chunk, op, offset, line);
        break;
    }

    out += line.str();
    return next;
}

std::string disassemble(const Chunk& chunk, const std::string& name) {
    std::string out = "== " + name + " ==\n";

    // walking by instruction, not by byte — an operand is not an opcode and
    // decoding one as if it were is how a disassembler starts printing garbage.
    std::size_t offset = 0;
    while (offset < chunk.size()) {
        offset = disassemble_instruction(chunk, offset, out);
        out += "\n";
    }
    return out;
}

} // namespace brewc
