#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

#include "brewc/chunk.h"
#include "brewc/value.h"

using namespace brewc;

TEST_CASE("a fresh chunk is empty", "[chunk]") {
    Chunk chunk;
    REQUIRE(chunk.size() == 0);
    REQUIRE(chunk.code.empty());
    REQUIRE(chunk.lines.empty());
    REQUIRE(chunk.constants.empty());
}

TEST_CASE("writing an opcode appends one byte", "[chunk]") {
    Chunk chunk;
    chunk.write(Opcode::Return, 1);
    REQUIRE(chunk.size() == 1);
    REQUIRE(chunk.code[0] == static_cast<uint8_t>(Opcode::Return));
}

TEST_CASE("an opcode and its operand are two separate bytes", "[chunk]") {
    // this is what a `Const 0` instruction looks like in the stream: the opcode
    // and then the index, with nothing marking the boundary between them.
    Chunk chunk;
    chunk.write(Opcode::Const, 1);
    chunk.write(uint8_t{0}, 1);
    REQUIRE(chunk.size() == 2);
    REQUIRE(chunk.code[0] == static_cast<uint8_t>(Opcode::Const));
    REQUIRE(chunk.code[1] == 0);
}

TEST_CASE("the line table stays the same length as the code", "[chunk]") {
    Chunk chunk;
    chunk.write(Opcode::Const, 3);
    chunk.write(uint8_t{0}, 3);
    chunk.write(Opcode::Negate, 4);
    REQUIRE(chunk.lines.size() == chunk.code.size());
}

TEST_CASE("line_at reports the line each byte came from", "[chunk]") {
    Chunk chunk;
    chunk.write(Opcode::Const, 7);
    chunk.write(uint8_t{0}, 7); // operand belongs to the same source line
    chunk.write(Opcode::Return, 9);
    REQUIRE(chunk.line_at(0) == 7);
    REQUIRE(chunk.line_at(1) == 7);
    REQUIRE(chunk.line_at(2) == 9);
}

TEST_CASE("line_at past the end gives 0 instead of reading out of bounds", "[chunk]") {
    Chunk chunk;
    chunk.write(Opcode::Return, 1);
    REQUIRE(chunk.line_at(1) == 0);
    REQUIRE(chunk.line_at(500) == 0);
}

TEST_CASE("every opcode has a name", "[chunk]") {
    REQUIRE(opcode_name(Opcode::Const) == "Const");
    REQUIRE(opcode_name(Opcode::Add) == "Add");
    REQUIRE(opcode_name(Opcode::JumpIfFalse) == "JumpIfFalse");
    REQUIRE(opcode_name(Opcode::Return) == "Return");
}

TEST_CASE("constants are added in order and handed back by index", "[chunk]") {
    Chunk chunk;
    REQUIRE(chunk.add_constant(int64_t{42}) == 0);
    REQUIRE(chunk.add_constant(std::string("hi")) == 1);
    REQUIRE(chunk.add_constant(3.5) == 2);
    REQUIRE(chunk.constants.size() == 3);

    REQUIRE(std::get<int64_t>(chunk.constant_at(0)) == 42);
    REQUIRE(std::get<std::string>(chunk.constant_at(1)) == "hi");
    REQUIRE(std::get<double>(chunk.constant_at(2)) == 3.5);
}

TEST_CASE("an equal constant reuses the slot it already has", "[chunk]") {
    // `let a = 1 let b = 1` shouldn't burn two slots on the same number.
    Chunk chunk;
    std::size_t first = chunk.add_constant(int64_t{1});
    std::size_t again = chunk.add_constant(int64_t{1});
    REQUIRE(first == again);
    REQUIRE(chunk.constants.size() == 1);

    REQUIRE(chunk.add_constant(std::string("brew")) == 1);
    REQUIRE(chunk.add_constant(std::string("brew")) == 1);
    REQUIRE(chunk.constants.size() == 2);
}

TEST_CASE("an int and a float that look alike stay separate constants", "[chunk]") {
    // 1 and 1.0 print almost the same but they are different types at runtime, so
    // sharing a slot would change what Const pushes.
    Chunk chunk;
    std::size_t as_int = chunk.add_constant(int64_t{1});
    std::size_t as_float = chunk.add_constant(1.0);
    REQUIRE(as_int != as_float);
    REQUIRE(is_int(chunk.constant_at(as_int)));
    REQUIRE(is_float(chunk.constant_at(as_float)));
}

TEST_CASE("the pool holds 256 constants and refuses the next one", "[chunk]") {
    // the ceiling isn't arbitrary — Const's operand is one byte, so an index past
    // 255 can't be written into the stream at all.
    Chunk chunk;
    for (std::size_t i = 0; i < Chunk::max_constants; ++i) {
        REQUIRE(chunk.add_constant(static_cast<int64_t>(i)) == i);
    }
    REQUIRE(chunk.constants.size() == Chunk::max_constants);

    REQUIRE(chunk.add_constant(int64_t{9999}) == Chunk::max_constants);
    REQUIRE(chunk.constants.size() == Chunk::max_constants);

    // a full pool can still reuse what's in it, only new values get turned away.
    REQUIRE(chunk.add_constant(int64_t{7}) == 7);
}

TEST_CASE("constant_at past the end gives nil instead of reading out of bounds",
          "[chunk]") {
    Chunk chunk;
    chunk.add_constant(int64_t{5});
    REQUIRE(is_nil(chunk.constant_at(1)));
    REQUIRE(is_nil(chunk.constant_at(500)));
}

TEST_CASE("a constant index fits in the operand byte", "[chunk]") {
    // what the compiler will actually do in step 65: stash the value, then write
    // its index as the byte after the opcode.
    Chunk chunk;
    std::size_t index = chunk.add_constant(int64_t{12});
    REQUIRE(index < Chunk::max_constants);

    chunk.write(Opcode::Const, 1);
    chunk.write(static_cast<uint8_t>(index), 1);

    REQUIRE(chunk.code[0] == static_cast<uint8_t>(Opcode::Const));
    REQUIRE(std::get<int64_t>(chunk.constant_at(chunk.code[1])) == 12);
}

TEST_CASE("opcodes fit in a single byte", "[chunk]") {
    // the whole design assumes an opcode round-trips through a uint8_t. if the
    // enum ever grows past 256 entries this is the test that should fail.
    REQUIRE(sizeof(Opcode) == 1);
    Opcode round_tripped = static_cast<Opcode>(static_cast<uint8_t>(Opcode::Loop));
    REQUIRE(round_tripped == Opcode::Loop);
}
