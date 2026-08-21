#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <variant>

#include "brewc/chunk.h"
#include "brewc/value.h"
#include "brewc/vm.h"

using namespace brewc;

namespace {

// put a value in the pool and write the Const that pushes it. every test below
// starts by getting something onto the stack, and doing it by hand is two lines
// and one chance to write the operand of one instruction as the opcode of the
// next.
//
// the compiler is not involved on purpose. it cannot emit a whole program yet,
// and testing the VM through it would mean a failure here could be either half.
void write_constant(Chunk& chunk, Value value, int line = 1) {
    std::size_t index = chunk.add_constant(std::move(value));
    REQUIRE(index < Chunk::max_constants);
    chunk.write(Opcode::Const, line);
    chunk.write(static_cast<uint8_t>(index), line);
}

} // namespace

TEST_CASE("an empty chunk stops right away", "[vm]") {
    // no Return to stop at, so this only ends if the loop notices it has run out
    // of instructions.
    Chunk chunk;
    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 0);
}

TEST_CASE("Const pushes the constant it names", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{42});
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 42);
}

TEST_CASE("Const works for every kind of pooled value", "[vm]") {
    SECTION("float") {
        Chunk chunk;
        write_constant(chunk, 3.5);
        chunk.write(Opcode::Return, 1);

        VM vm;
        REQUIRE(vm.run(chunk) == InterpretResult::Ok);
        REQUIRE(std::get<double>(vm.stack_top()) == 3.5);
    }

    SECTION("string") {
        Chunk chunk;
        write_constant(chunk, std::string("hello"));
        chunk.write(Opcode::Return, 1);

        VM vm;
        REQUIRE(vm.run(chunk) == InterpretResult::Ok);
        REQUIRE(std::get<std::string>(vm.stack_top()) == "hello");
    }
}

TEST_CASE("each Const leaves its own value behind", "[vm]") {
    // the second push does not replace the first. that is the whole reason this
    // is a stack and not a single result slot like the tree-walker has.
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    write_constant(chunk, int64_t{2});
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 2);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 2);
}

TEST_CASE("the same constant twice pushes twice from one slot", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{7});
    write_constant(chunk, int64_t{7});
    chunk.write(Opcode::Return, 1);

    // add_constant hands back the slot it already had, so both instructions name
    // index 0 and the pool stays at one entry.
    REQUIRE(chunk.constants.size() == 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 2);
}

TEST_CASE("Return stops without touching the stack", "[vm]") {
    // the instruction after the Return would push a second value if it ran.
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    chunk.write(Opcode::Return, 1);
    write_constant(chunk, int64_t{2}, 2);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 1);
}

TEST_CASE("the top of an empty stack reads as nil", "[vm]") {
    Chunk chunk;
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 0);
    REQUIRE(is_nil(vm.stack_top()));
}

TEST_CASE("an opcode this step does not run is an error", "[vm]") {
    // Pop is a real instruction with no case in the dispatch loop until step 75.
    // stopping is the point: skipping it would leave the stack a value deeper
    // than the compiler thinks it is and every later instruction would read the
    // wrong slot.
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    chunk.write(Opcode::Pop, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
}

TEST_CASE("arithmetic on two ints", "[vm]") {
    // one helper for all four, since the only thing that changes between them is
    // the opcode and the number that should come out.
    auto run_ints = [](Opcode op, int64_t a, int64_t b) {
        Chunk chunk;
        write_constant(chunk, a);
        write_constant(chunk, b);
        chunk.write(op, 1);
        chunk.write(Opcode::Return, 1);

        VM vm;
        REQUIRE(vm.run(chunk) == InterpretResult::Ok);
        REQUIRE(vm.stack_size() == 1);
        return std::get<int64_t>(vm.stack_top());
    };

    REQUIRE(run_ints(Opcode::Add, 2, 3) == 5);
    REQUIRE(run_ints(Opcode::Sub, 2, 3) == -1);
    REQUIRE(run_ints(Opcode::Mul, 4, 5) == 20);
    REQUIRE(run_ints(Opcode::Mod, 7, 2) == 1);
}

TEST_CASE("the operands come off in the right order", "[vm]") {
    // the one that catches a swapped pop. 2 + 3 is 5 either way round, and so is
    // 4 * 5, but 2 - 3 is only -1 if the value pushed first is the left operand.
    Chunk chunk;
    write_constant(chunk, int64_t{10});
    write_constant(chunk, int64_t{3});
    chunk.write(Opcode::Sub, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 7);
}

TEST_CASE("dividing two ints truncates and does not promote", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{7});
    write_constant(chunk, int64_t{2});
    chunk.write(Opcode::Div, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(is_int(vm.stack_top()));
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 3);
}

TEST_CASE("one float in the pair widens the other", "[vm]") {
    // same two numbers as the case above, so the only difference in the answer
    // comes from the .0 the source had.
    Chunk chunk;
    write_constant(chunk, int64_t{7});
    write_constant(chunk, 2.0);
    chunk.write(Opcode::Div, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(is_float(vm.stack_top()));
    REQUIRE(std::get<double>(vm.stack_top()) == 3.5);
}

TEST_CASE("adding two strings joins them", "[vm]") {
    Chunk chunk;
    write_constant(chunk, std::string("he"));
    write_constant(chunk, std::string("llo"));
    chunk.write(Opcode::Add, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(std::get<std::string>(vm.stack_top()) == "hello");
}

TEST_CASE("only + means anything between two strings", "[vm]") {
    Chunk chunk;
    write_constant(chunk, std::string("he"));
    write_constant(chunk, std::string("llo"));
    chunk.write(Opcode::Sub, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
}

TEST_CASE("arithmetic on something that is not a number stops the run", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    write_constant(chunk, std::string("two"));
    chunk.write(Opcode::Add, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
}

TEST_CASE("dividing an int by zero is an error and not undefined behaviour", "[vm]") {
    SECTION("Div") {
        Chunk chunk;
        write_constant(chunk, int64_t{1});
        write_constant(chunk, int64_t{0});
        chunk.write(Opcode::Div, 1);
        chunk.write(Opcode::Return, 1);

        VM vm;
        REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    }

    SECTION("Mod") {
        Chunk chunk;
        write_constant(chunk, int64_t{1});
        write_constant(chunk, int64_t{0});
        chunk.write(Opcode::Mod, 1);
        chunk.write(Opcode::Return, 1);

        VM vm;
        REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    }
}

TEST_CASE("dividing a float by zero follows IEEE instead of stopping", "[vm]") {
    // the int guard above is there because the hardware traps, not because the
    // language wants to reject it. floats have inf for this, so the tree-walker
    // lets it through and so does the VM.
    Chunk chunk;
    write_constant(chunk, 1.0);
    write_constant(chunk, 0.0);
    chunk.write(Opcode::Div, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(std::isinf(std::get<double>(vm.stack_top())));
}

TEST_CASE("modulo rejects floats", "[vm]") {
    Chunk chunk;
    write_constant(chunk, 7.5);
    write_constant(chunk, 2.0);
    chunk.write(Opcode::Mod, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
}

TEST_CASE("Negate flips the sign of one value", "[vm]") {
    SECTION("int stays an int") {
        Chunk chunk;
        write_constant(chunk, int64_t{5});
        chunk.write(Opcode::Negate, 1);
        chunk.write(Opcode::Return, 1);

        VM vm;
        REQUIRE(vm.run(chunk) == InterpretResult::Ok);
        REQUIRE(vm.stack_size() == 1);
        REQUIRE(is_int(vm.stack_top()));
        REQUIRE(std::get<int64_t>(vm.stack_top()) == -5);
    }

    SECTION("float") {
        Chunk chunk;
        write_constant(chunk, 2.5);
        chunk.write(Opcode::Negate, 1);
        chunk.write(Opcode::Return, 1);

        VM vm;
        REQUIRE(vm.run(chunk) == InterpretResult::Ok);
        REQUIRE(std::get<double>(vm.stack_top()) == -2.5);
    }

    SECTION("the most negative int wraps instead of being undefined") {
        Chunk chunk;
        write_constant(chunk, std::numeric_limits<int64_t>::min());
        chunk.write(Opcode::Negate, 1);
        chunk.write(Opcode::Return, 1);

        VM vm;
        REQUIRE(vm.run(chunk) == InterpretResult::Ok);
        REQUIRE(std::get<int64_t>(vm.stack_top()) == std::numeric_limits<int64_t>::min());
    }

    SECTION("anything that is not a number stops the run") {
        Chunk chunk;
        write_constant(chunk, std::string("five"));
        chunk.write(Opcode::Negate, 1);
        chunk.write(Opcode::Return, 1);

        VM vm;
        REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    }
}

TEST_CASE("the comparison opcodes push a bool", "[vm]") {
    // one helper for all four, since the only thing that changes between them is
    // the byte in the middle.
    auto compare_ints = [](int64_t a, int64_t b, Opcode op) {
        Chunk chunk;
        write_constant(chunk, a);
        write_constant(chunk, b);
        chunk.write(op, 1);
        chunk.write(Opcode::Return, 1);

        VM vm;
        REQUIRE(vm.run(chunk) == InterpretResult::Ok);
        REQUIRE(vm.stack_size() == 1);
        REQUIRE(is_bool(vm.stack_top()));
        return std::get<bool>(vm.stack_top());
    };

    SECTION("Less and Greater") {
        REQUIRE(compare_ints(1, 2, Opcode::Less));
        REQUIRE_FALSE(compare_ints(2, 1, Opcode::Less));
        REQUIRE_FALSE(compare_ints(2, 2, Opcode::Less));
        REQUIRE(compare_ints(2, 1, Opcode::Greater));
        REQUIRE_FALSE(compare_ints(1, 2, Opcode::Greater));
    }

    SECTION("Equal and NotEqual") {
        REQUIRE(compare_ints(3, 3, Opcode::Equal));
        REQUIRE_FALSE(compare_ints(3, 4, Opcode::Equal));
        REQUIRE(compare_ints(3, 4, Opcode::NotEqual));
        REQUIRE_FALSE(compare_ints(3, 3, Opcode::NotEqual));
    }

    SECTION("the operands do not come off backwards") {
        // 1 < 2 popped in the wrong order is 2 < 1, and both answers are a bool,
        // so nothing but the value itself catches this.
        REQUIRE(compare_ints(1, 2, Opcode::Less));
    }
}

TEST_CASE("<= and >= are a comparison plus a Not", "[vm]") {
    // the compiler has no opcode for either one, so this is the shape it emits.
    auto run_pair = [](int64_t a, int64_t b, Opcode op) {
        Chunk chunk;
        write_constant(chunk, a);
        write_constant(chunk, b);
        chunk.write(op, 1);
        chunk.write(Opcode::Not, 1);
        chunk.write(Opcode::Return, 1);

        VM vm;
        REQUIRE(vm.run(chunk) == InterpretResult::Ok);
        return std::get<bool>(vm.stack_top());
    };

    // a <= b is not (a > b)
    REQUIRE(run_pair(2, 2, Opcode::Greater));
    REQUIRE(run_pair(1, 2, Opcode::Greater));
    REQUIRE_FALSE(run_pair(3, 2, Opcode::Greater));

    // a >= b is not (a < b)
    REQUIRE(run_pair(2, 2, Opcode::Less));
    REQUIRE(run_pair(3, 2, Opcode::Less));
    REQUIRE_FALSE(run_pair(1, 2, Opcode::Less));
}

TEST_CASE("comparing an int against a float widens instead of failing", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    write_constant(chunk, 1.5);
    chunk.write(Opcode::Less, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(std::get<bool>(vm.stack_top()));
}

TEST_CASE("two ints are compared as ints", "[vm]") {
    // both of these are the same double once they go through to_double, so if the
    // int path were not there they would come back equal.
    int64_t big = (int64_t{1} << 53) + 1;

    Chunk chunk;
    write_constant(chunk, big);
    write_constant(chunk, big - 1);
    chunk.write(Opcode::Greater, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(std::get<bool>(vm.stack_top()));
}

TEST_CASE("equality works across kinds without ordering doing the same", "[vm]") {
    auto run_op = [](Value lhs, Value rhs, Opcode op) {
        Chunk chunk;
        write_constant(chunk, std::move(lhs));
        write_constant(chunk, std::move(rhs));
        chunk.write(op, 1);
        chunk.write(Opcode::Return, 1);

        VM vm;
        InterpretResult result = vm.run(chunk);
        return std::make_pair(result, vm.stack_top());
    };

    SECTION("two strings can be equal") {
        auto [result, value] = run_op(std::string("hi"), std::string("hi"), Opcode::Equal);
        REQUIRE(result == InterpretResult::Ok);
        REQUIRE(std::get<bool>(value));
    }

    SECTION("different kinds are never equal") {
        auto [result, value] = run_op(int64_t{1}, std::string("1"), Opcode::Equal);
        REQUIRE(result == InterpretResult::Ok);
        REQUIRE_FALSE(std::get<bool>(value));
    }

    SECTION("bools compare by their value") {
        auto [result, value] = run_op(true, false, Opcode::NotEqual);
        REQUIRE(result == InterpretResult::Ok);
        REQUIRE(std::get<bool>(value));
    }

    SECTION("ordering two strings stops the run") {
        auto [result, value] = run_op(std::string("a"), std::string("b"), Opcode::Less);
        (void)value;
        REQUIRE(result == InterpretResult::RuntimeError);
    }

    SECTION("ordering a bool against a number stops the run") {
        auto [result, value] = run_op(true, int64_t{1}, Opcode::Greater);
        (void)value;
        REQUIRE(result == InterpretResult::RuntimeError);
    }
}

TEST_CASE("Not follows the same truthiness as the tree-walker", "[vm]") {
    auto negated = [](Value value) {
        Chunk chunk;
        write_constant(chunk, std::move(value));
        chunk.write(Opcode::Not, 1);
        chunk.write(Opcode::Return, 1);

        VM vm;
        REQUIRE(vm.run(chunk) == InterpretResult::Ok);
        REQUIRE(vm.stack_size() == 1);
        REQUIRE(is_bool(vm.stack_top()));
        return std::get<bool>(vm.stack_top());
    };

    // only nil and false are falsy, so everything else here inverts to false —
    // zero and the empty string included.
    REQUIRE(negated(Nil{}));
    REQUIRE(negated(false));
    REQUIRE_FALSE(negated(true));
    REQUIRE_FALSE(negated(int64_t{0}));
    REQUIRE_FALSE(negated(int64_t{7}));
    REQUIRE_FALSE(negated(0.0));
    REQUIRE_FALSE(negated(std::string("")));
}

TEST_CASE("two Nots cancel out", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{5});
    chunk.write(Opcode::Not, 1);
    chunk.write(Opcode::Not, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
    REQUIRE(std::get<bool>(vm.stack_top()));
}

TEST_CASE("a nested expression leaves one value behind", "[vm]") {
    // (1 + 2) * (10 - 4), the shape the compiler emits for it. the point is not
    // the 18 so much as the stack being one deep at the end: every operator took
    // two off and put one back, however deep the tree was.
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    write_constant(chunk, int64_t{2});
    chunk.write(Opcode::Add, 1);
    write_constant(chunk, int64_t{10});
    write_constant(chunk, int64_t{4});
    chunk.write(Opcode::Sub, 1);
    chunk.write(Opcode::Mul, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 18);
}

TEST_CASE("a Const whose operand byte is missing does not run off the end", "[vm]") {
    // a truncated chunk. only a hand-written one can look like this, but reading
    // past the code vector to find the index would be undefined behaviour rather
    // than a wrong answer.
    Chunk chunk;
    chunk.add_constant(int64_t{9});
    chunk.write(Opcode::Const, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
}

TEST_CASE("running a second chunk starts from a clean stack", "[vm]") {
    Chunk first;
    write_constant(first, int64_t{1});
    first.write(Opcode::Return, 1);

    Chunk second;
    write_constant(second, int64_t{2});
    second.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(first) == InterpretResult::Ok);
    REQUIRE(vm.run(second) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 2);
}

TEST_CASE("the same chunk can be run more than once", "[vm]") {
    // nothing in run() writes to the chunk, which is what makes a compiled
    // program worth keeping around instead of recompiling it.
    Chunk chunk;
    write_constant(chunk, int64_t{5});
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 5);
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 5);
}
