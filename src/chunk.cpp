#include "brewc/chunk.h"

#include <utility>

namespace brewc {

namespace {

// true when the pool can hand out an existing slot instead of adding one.
//
// only the kinds that actually end up in a pool are compared. nil, true and false
// each have their own opcode so they never get here, and two functions are only
// the same function if they came from the same declaration and the same scope,
// which is more than a literal comparison should be deciding.
//
// two floats being equal is exact bit-for-bit equality, near enough — 0.1 and
// 0.1 written twice in the source parse to the same double. NaN never matches
// itself so a program full of NaN literals gets a slot each, which nobody will
// notice.
bool same_constant(const Value& a, const Value& b) {
    if (is_int(a) && is_int(b)) return std::get<int64_t>(a) == std::get<int64_t>(b);
    if (is_float(a) && is_float(b)) return std::get<double>(a) == std::get<double>(b);
    if (is_string(a) && is_string(b)) {
        return std::get<std::string>(a) == std::get<std::string>(b);
    }
    return false;
}

} // namespace

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

std::size_t Chunk::add_constant(Value value) {
    // a linear scan, which is quadratic over a whole chunk, but the pool tops out
    // at 256 entries so the worst case is small and it saves keeping a second
    // lookup table in sync.
    for (std::size_t i = 0; i < constants.size(); ++i) {
        if (same_constant(constants[i], value)) return i;
    }

    if (constants.size() >= max_constants) return max_constants;

    constants.push_back(std::move(value));
    return constants.size() - 1;
}

const Value& Chunk::constant_at(std::size_t index) const {
    static const Value out_of_range = Nil{};
    if (index >= constants.size()) return out_of_range;
    return constants[index];
}

} // namespace brewc
