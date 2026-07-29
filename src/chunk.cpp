#include "brewc/chunk.h"

namespace brewc {

std::string opcode_name(Opcode op) {
    switch (op) {
    case Opcode::Const: return "Const";
    case Opcode::Nil: return "Nil";
    case Opcode::True: return "True";
    case Opcode::False: return "False";
    case Opcode::Add: return "Add";
    case Opcode::Sub: return "Sub";
    case Opcode::Mul: return "Mul";
    case Opcode::Div: return "Div";
    case Opcode::Mod: return "Mod";
    case Opcode::Negate: return "Negate";
    case Opcode::Equal: return "Equal";
    case Opcode::NotEqual: return "NotEqual";
    case Opcode::Less: return "Less";
    case Opcode::Greater: return "Greater";
    case Opcode::Not: return "Not";
    case Opcode::GetLocal: return "GetLocal";
    case Opcode::SetLocal: return "SetLocal";
    case Opcode::Pop: return "Pop";
    case Opcode::Jump: return "Jump";
    case Opcode::JumpIfFalse: return "JumpIfFalse";
    case Opcode::Loop: return "Loop";
    case Opcode::Call: return "Call";
    case Opcode::Return: return "Return";
    }

    // no default label above, so -Wswitch complains the moment a new opcode is
    // added and not handled. this is only here for a byte that got cast into an
    // Opcode without being one.
    return "<unknown>";
}

void Chunk::write(uint8_t byte, int line) {
    code.push_back(byte);
    lines.push_back(line);
}

void Chunk::write(Opcode op, int line) { write(static_cast<uint8_t>(op), line); }

std::size_t Chunk::size() const { return code.size(); }

int Chunk::line_at(std::size_t offset) const {
    if (offset >= lines.size()) return 0;
    return lines[offset];
}

} // namespace brewc
