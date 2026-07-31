#include "brewc/disassembler.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace brewc {

namespace {

// widths for the columns. the opcode column is wide enough for JumpIfFalse, the
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

// Const, and its operand is an index into the constant pool. the index alone is
// not much use when you're trying to see whether the right literal got stored, so
// the value it points at comes along in quotes.
std::size_t constant(const Chunk& chunk, Opcode op, std::size_t offset, std::ostringstream& out) {
    uint8_t index = chunk.code[offset + 1];
    write_prefix(chunk, offset, out);
    write_name(op, out);
    out << std::setw(4) << static_cast<int>(index) << " '"
        << to_string(chunk.constant_at(index)) << "'";
    return offset + 2;
}

// GetLocal, SetLocal and Call. all take one byte that means a number — a stack
// slot for the first two, an argument count for Call — and there's nothing to
// look up, so the byte is printed on its own.
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

// how many bytes an opcode takes up in total, itself included. used to check the
// stream has them before reading, so a chunk that was cut short mid-instruction
// gets a complaint instead of an out of bounds read.
std::size_t instruction_length(Opcode op) {
    switch (op) {
    case Opcode::Const:
    case Opcode::GetLocal:
    case Opcode::SetLocal:
    case Opcode::Call:
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
    if (offset + instruction_length(op) > chunk.size()) {
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
        next = constant(chunk, op, offset, line);
        break;
    case Opcode::GetLocal:
    case Opcode::SetLocal:
    case Opcode::Call:
        next = byte_operand(chunk, op, offset, line);
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
