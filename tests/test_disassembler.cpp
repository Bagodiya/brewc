#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

#include "brewc/chunk.h"
#include "brewc/disassembler.h"

using namespace brewc;

namespace {

// disassemble one instruction and give back just its line, so a test can check a
// single instruction without the header and the newline getting in the way.
std::string line_at_offset(const Chunk& chunk, std::size_t offset) {
    std::string out;
    disassemble_instruction(chunk, offset, out);
    return out;
}

// true when text has needle somewhere in it. the columns are padded so checking
// for the whole line means counting spaces by hand, and that test would break the
// first time a column width changes for a reason nobody cares about.
bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

std::size_t count_lines(const std::string& text) {
    std::size_t lines = 0;
    for (char c : text) {
        if (c == '\n') ++lines;
    }
    return lines;
}

} // namespace

TEST_CASE("an empty chunk disassembles to just the header", "[disassembler]") {
    Chunk chunk;
    REQUIRE(disassemble(chunk, "main") == "== main ==\n");
}

TEST_CASE("the header carries the name it was given", "[disassembler]") {
    Chunk chunk;
    REQUIRE(contains(disassemble(chunk, "fib"), "== fib =="));
}

TEST_CASE("an instruction with no operands prints its offset and name",
          "[disassembler]") {
    Chunk chunk;
    chunk.write(Opcode::Return, 1);
    std::string line = line_at_offset(chunk, 0);
    REQUIRE(contains(line, "0000"));
    REQUIRE(contains(line, "Return"));
}

TEST_CASE("a bare instruction is one byte long", "[disassembler]") {
    Chunk chunk;
    chunk.write(Opcode::Add, 1);
    std::string out;
    REQUIRE(disassemble_instruction(chunk, 0, out) == 1);
}

TEST_CASE("Const shows the pool index and the value behind it", "[disassembler]") {
    Chunk chunk;
    std::size_t index = chunk.add_constant(int64_t{42});
    chunk.write(Opcode::Const, 1);
    chunk.write(static_cast<uint8_t>(index), 1);

    std::string line = line_at_offset(chunk, 0);
    REQUIRE(contains(line, "Const"));
    REQUIRE(contains(line, "0"));
    REQUIRE(contains(line, "'42'"));
}

TEST_CASE("Const skips over its operand byte", "[disassembler]") {
    // the point of returning the next offset: byte 1 is an index, not an opcode,
    // and decoding it as one would print nonsense from here on.
    Chunk chunk;
    chunk.add_constant(int64_t{7});
    chunk.write(Opcode::Const, 1);
    chunk.write(uint8_t{0}, 1);

    std::string out;
    REQUIRE(disassemble_instruction(chunk, 0, out) == 2);
}

TEST_CASE("a string constant prints its text", "[disassembler]") {
    Chunk chunk;
    std::size_t index = chunk.add_constant(std::string("brew"));
    chunk.write(Opcode::Const, 2);
    chunk.write(static_cast<uint8_t>(index), 2);
    REQUIRE(contains(line_at_offset(chunk, 0), "'brew'"));
}

TEST_CASE("locals print the slot number they use", "[disassembler]") {
    Chunk chunk;
    chunk.write(Opcode::GetLocal, 3);
    chunk.write(uint8_t{2}, 3);

    std::string out;
    std::size_t next = disassemble_instruction(chunk, 0, out);
    REQUIRE(contains(out, "GetLocal"));
    REQUIRE(contains(out, "2"));
    REQUIRE(next == 2);
}

TEST_CASE("Call prints how many arguments it takes", "[disassembler]") {
    Chunk chunk;
    chunk.write(Opcode::Call, 5);
    chunk.write(uint8_t{3}, 5);
    REQUIRE(contains(line_at_offset(chunk, 0), "Call"));
    REQUIRE(contains(line_at_offset(chunk, 0), "3"));
}

TEST_CASE("a forward jump prints where it lands", "[disassembler]") {
    // JumpIfFalse at 0 is three bytes, so counting 4 forward from the instruction
    // after it puts the target at 7.
    Chunk chunk;
    chunk.write(Opcode::JumpIfFalse, 1);
    chunk.write(uint8_t{0}, 1);
    chunk.write(uint8_t{4}, 1);

    std::string out;
    std::size_t next = disassemble_instruction(chunk, 0, out);
    REQUIRE(contains(out, "JumpIfFalse"));
    REQUIRE(contains(out, "-> 7"));
    REQUIRE(next == 3);
}

TEST_CASE("a jump offset uses both of its bytes", "[disassembler]") {
    // 0x01 0x2c is 300, which is the whole reason the operand is two bytes wide.
    Chunk chunk;
    chunk.write(Opcode::Jump, 1);
    chunk.write(uint8_t{1}, 1);
    chunk.write(uint8_t{44}, 1);
    REQUIRE(contains(line_at_offset(chunk, 0), "300"));
    REQUIRE(contains(line_at_offset(chunk, 0), "-> 303"));
}

TEST_CASE("Loop counts backwards instead of forwards", "[disassembler]") {
    // a while loop jumps back to its condition. same operand encoding as Jump,
    // opposite direction.
    Chunk chunk;
    chunk.write(Opcode::Pop, 1);
    chunk.write(Opcode::Pop, 1);
    chunk.write(Opcode::Loop, 2);
    chunk.write(uint8_t{0}, 2);
    chunk.write(uint8_t{5}, 2);

    REQUIRE(contains(line_at_offset(chunk, 2), "Loop"));
    REQUIRE(contains(line_at_offset(chunk, 2), "-> 0"));
}

TEST_CASE("the line column repeats as a bar within one source line",
          "[disassembler]") {
    Chunk chunk;
    chunk.write(Opcode::Const, 1);
    chunk.write(uint8_t{0}, 1);
    chunk.write(Opcode::Negate, 1); // same source line
    chunk.write(Opcode::Return, 2); // new one

    chunk.add_constant(int64_t{1});
    REQUIRE(contains(line_at_offset(chunk, 0), "1"));
    REQUIRE(contains(line_at_offset(chunk, 2), "|"));
    REQUIRE(!contains(line_at_offset(chunk, 3), "|"));
    REQUIRE(contains(line_at_offset(chunk, 3), "2"));
}

TEST_CASE("the first instruction always shows its line number", "[disassembler]") {
    Chunk chunk;
    chunk.write(Opcode::Return, 9);
    REQUIRE(contains(line_at_offset(chunk, 0), "9"));
    REQUIRE(!contains(line_at_offset(chunk, 0), "|"));
}

TEST_CASE("a full chunk gives one line per instruction", "[disassembler]") {
    // three instructions over five bytes, so counting bytes instead of decoding
    // would print five lines here.
    Chunk chunk;
    std::size_t index = chunk.add_constant(int64_t{2});
    chunk.write(Opcode::Const, 1);
    chunk.write(static_cast<uint8_t>(index), 1);
    chunk.write(Opcode::Negate, 1);
    chunk.write(Opcode::Call, 1);
    chunk.write(uint8_t{1}, 1);

    std::string text = disassemble(chunk, "main");
    REQUIRE(count_lines(text) == 4); // header plus three instructions
    REQUIRE(contains(text, "Const"));
    REQUIRE(contains(text, "Negate"));
    REQUIRE(contains(text, "Call"));
}

TEST_CASE("offsets in the dump follow the byte positions", "[disassembler]") {
    Chunk chunk;
    chunk.add_constant(int64_t{5});
    chunk.write(Opcode::Const, 1);
    chunk.write(uint8_t{0}, 1);
    chunk.write(Opcode::Return, 1);

    std::string text = disassemble(chunk, "main");
    REQUIRE(contains(text, "0000"));
    REQUIRE(contains(text, "0002")); // Return, not 0001
}

TEST_CASE("a byte that is not an opcode is reported, not decoded",
          "[disassembler]") {
    Chunk chunk;
    chunk.write(uint8_t{200}, 1);
    std::string line = line_at_offset(chunk, 0);
    REQUIRE(contains(line, "unknown"));
    REQUIRE(contains(line, "200"));
}

TEST_CASE("an instruction missing its operand says so instead of reading past the end",
          "[disassembler]") {
    // half-written bytecode is exactly the situation this tool exists for, so it
    // has to survive it.
    Chunk chunk;
    chunk.write(Opcode::Const, 1); // no index byte after it

    std::string line = line_at_offset(chunk, 0);
    REQUIRE(contains(line, "Const"));
    REQUIRE(contains(line, "truncated"));
}

TEST_CASE("a truncated jump stops the walk instead of looping", "[disassembler]") {
    Chunk chunk;
    chunk.write(Opcode::Jump, 1);
    chunk.write(uint8_t{0}, 1); // second offset byte never written

    std::string out;
    REQUIRE(disassemble_instruction(chunk, 0, out) >= chunk.size());
    REQUIRE(count_lines(disassemble(chunk, "main")) == 2);
}

TEST_CASE("stepping past the end says so rather than crashing", "[disassembler]") {
    Chunk chunk;
    chunk.write(Opcode::Return, 1);

    std::string out;
    std::size_t next = disassemble_instruction(chunk, 5, out);
    REQUIRE(contains(out, "past end"));
    REQUIRE(next > 5);
}

TEST_CASE("a global instruction prints the name and not just the index",
          "[disassembler]") {
    // the index on its own tells you nothing. this is the reason the globals go
    // through the constant printer instead of the plain byte one.
    Chunk chunk;
    std::size_t index = chunk.add_constant(std::string("counter"));
    chunk.write(Opcode::GetGlobal, 1);
    chunk.write(static_cast<uint8_t>(index), 1);

    std::string line = line_at_offset(chunk, 0);
    REQUIRE(contains(line, "GetGlobal"));
    REQUIRE(contains(line, "'counter'"));
}

TEST_CASE("all three global opcodes decode the same way", "[disassembler]") {
    Chunk chunk;
    std::size_t index = chunk.add_constant(std::string("x"));
    for (Opcode op : {Opcode::DefineGlobal, Opcode::GetGlobal, Opcode::SetGlobal}) {
        chunk.write(op, 1);
        chunk.write(static_cast<uint8_t>(index), 1);
    }

    std::string out = disassemble(chunk, "main");
    REQUIRE(contains(out, "DefineGlobal"));
    REQUIRE(contains(out, "SetGlobal"));
    // one header line plus one per instruction, so the walk stepped over the
    // operand bytes instead of trying to decode them as opcodes.
    REQUIRE(count_lines(out) == 4);
}

TEST_CASE("a global takes two bytes so the next instruction lands at 2",
          "[disassembler]") {
    Chunk chunk;
    std::size_t index = chunk.add_constant(std::string("x"));
    chunk.write(Opcode::DefineGlobal, 1);
    chunk.write(static_cast<uint8_t>(index), 1);
    chunk.write(Opcode::Return, 1);

    std::string out;
    REQUIRE(disassemble_instruction(chunk, 0, out) == 2);
}

TEST_CASE("a global missing its operand says so instead of reading past the end",
          "[disassembler]") {
    Chunk chunk;
    chunk.write(Opcode::GetGlobal, 1); // no index byte after it

    std::string line = line_at_offset(chunk, 0);
    REQUIRE(contains(line, "GetGlobal"));
    REQUIRE(contains(line, "truncated"));
}
