#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "brewc/chunk.h"

using namespace brewc;

TEST_CASE("a fresh chunk is empty", "[chunk]") {
    Chunk chunk;
    REQUIRE(chunk.size() == 0);
    REQUIRE(chunk.code.empty());
    REQUIRE(chunk.lines.empty());
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

TEST_CASE("opcodes fit in a single byte", "[chunk]") {
    // the whole design assumes an opcode round-trips through a uint8_t. if the
    // enum ever grows past 256 entries this is the test that should fail.
    REQUIRE(sizeof(Opcode) == 1);
    Opcode round_tripped = static_cast<Opcode>(static_cast<uint8_t>(Opcode::Loop));
    REQUIRE(round_tripped == Opcode::Loop);
}
