#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <variant>

#include "brewc/chunk.h"
#include "brewc/runtime_error.h"
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

// the same thing for a name, plus whichever global opcode wants it. the operand
// is an index into the pool and not the name itself, so writing one of these by
// hand is two lines and a chance to point at the wrong constant.
void write_global(Chunk& chunk, Opcode op, const std::string& name, int line = 1) {
    std::size_t index = chunk.add_constant(name);
    REQUIRE(index < Chunk::max_constants);
    chunk.write(op, line);
    chunk.write(static_cast<uint8_t>(index), line);
}

// a slot opcode and the byte that says which slot. the operand is a position on
// the stack rather than an index into the pool, so unlike write_global there is
// nothing to add anywhere first — the value has to have been pushed already.
void write_local(Chunk& chunk, Opcode op, uint8_t slot, int line = 1) {
    chunk.write(op, line);
    chunk.write(slot, line);
}

// a jump and its two operand bytes, high byte first. the distance is counted
// from the instruction after this one, which is the part that is easy to be off
// by three on when writing the bytes out by hand.
void write_jump(Chunk& chunk, Opcode op, uint16_t distance, int line = 1) {
    chunk.write(op, line);
    chunk.write(static_cast<uint8_t>((distance >> 8) & 0xff), line);
    chunk.write(static_cast<uint8_t>(distance & 0xff), line);
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
    // Call is a real instruction with no case in the dispatch loop until step 82.
    // stopping is the point: skipping it would leave the stack a value shallower
    // than the compiler thinks it is and every later instruction would read the
    // wrong slot.
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    chunk.write(Opcode::Call, 1);
    chunk.write(static_cast<uint8_t>(0), 1);
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

TEST_CASE("a failed run leaves an error behind with the line on it", "[vm]") {
    // the Div is written as line 3, so that is the line the report has to name.
    // the two constants are on line 3 as well — anything else and a lookup that
    // was off by a byte would still read as line 3 and the test would pass
    // without checking anything.
    Chunk chunk;
    write_constant(chunk, int64_t{1}, 3);
    write_constant(chunk, int64_t{0}, 3);
    chunk.write(Opcode::Div, 3);
    chunk.write(Opcode::Return, 3);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);

    const RuntimeError* err = vm.error();
    REQUIRE(err != nullptr);
    REQUIRE(std::string(err->what()) == "division by zero");
    REQUIRE(err->line() == 3);
}

TEST_CASE("the error names the line of the instruction that failed", "[vm]") {
    // one line per instruction, so a lookup that lands on the neighbouring byte
    // comes back with a different number instead of the same one.
    Chunk chunk;
    write_constant(chunk, int64_t{1}, 10);
    write_constant(chunk, std::string("two"), 11);
    chunk.write(Opcode::Add, 12);
    chunk.write(Opcode::Return, 13);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.error() != nullptr);
    REQUIRE(vm.error()->line() == 12);
}

TEST_CASE("a run that goes fine leaves no error", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{7});
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.error() == nullptr);
}

TEST_CASE("a good run clears the error the last one left", "[vm]") {
    // the repl reuses one VM for the whole session. a line that works after a
    // line that did not has to read as working.
    Chunk bad;
    write_constant(bad, int64_t{1}, 1);
    write_constant(bad, int64_t{0}, 1);
    bad.write(Opcode::Div, 1);

    Chunk good;
    write_constant(good, int64_t{4}, 1);
    good.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(bad) == InterpretResult::RuntimeError);
    REQUIRE(vm.error() != nullptr);

    REQUIRE(vm.run(good) == InterpretResult::Ok);
    REQUIRE(vm.error() == nullptr);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 4);
}

TEST_CASE("the stack is empty after a failed run", "[vm]") {
    // the Add pops both of its operands before it throws, but the 1 pushed ahead
    // of the pair is still there when it does. whatever a run leaves half done is
    // the next run's problem unless it gets cleaned up here.
    Chunk chunk;
    write_constant(chunk, int64_t{1}, 1);
    write_constant(chunk, int64_t{2}, 1);
    write_constant(chunk, std::string("three"), 1);
    chunk.write(Opcode::Add, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.stack_size() == 0);
}

TEST_CASE("the VM says the same thing about a bad pair as the tree-walker", "[vm]") {
    // the message quotes the operator the user wrote and not the name of the
    // opcode, so `1 + "two"` reads the same whichever backend ran it.
    Chunk chunk;
    write_constant(chunk, int64_t{1}, 1);
    write_constant(chunk, std::string("two"), 1);
    chunk.write(Opcode::Add, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.error() != nullptr);
    REQUIRE(std::string(vm.error()->what()) == "cannot apply '+' to int and string");
}

TEST_CASE("ordering two strings reports what could not be ordered", "[vm]") {
    Chunk chunk;
    write_constant(chunk, std::string("a"), 2);
    write_constant(chunk, std::string("b"), 2);
    chunk.write(Opcode::Less, 2);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.error() != nullptr);
    REQUIRE(std::string(vm.error()->what()) == "cannot order string and string");
    REQUIRE(vm.error()->line() == 2);
}

TEST_CASE("negating something that is not a number reports it", "[vm]") {
    Chunk chunk;
    write_constant(chunk, std::string("x"), 5);
    chunk.write(Opcode::Negate, 6);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.error() != nullptr);
    REQUIRE(std::string(vm.error()->what()) == "cannot negate string");
    REQUIRE(vm.error()->line() == 6);
}

TEST_CASE("an opcode with no case yet says which one it was", "[vm]") {
    // Call has no case until step 82. the message is about the VM and not about
    // the program, but a stop with nothing to say is worse to run into.
    Chunk chunk;
    write_constant(chunk, int64_t{1}, 4);
    chunk.write(Opcode::Call, 4);
    chunk.write(static_cast<uint8_t>(0), 4);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.error() != nullptr);
    REQUIRE(std::string(vm.error()->what()) == "Call is not implemented yet");
    REQUIRE(vm.error()->line() == 4);
}

TEST_CASE("a VM error prints the same way an interpreter error does", "[vm]") {
    // format_error is what the driver calls on either backend, so it has to take
    // one of these without any special casing.
    Chunk chunk;
    write_constant(chunk, int64_t{1}, 2);
    write_constant(chunk, int64_t{0}, 2);
    chunk.write(Opcode::Div, 2);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.error() != nullptr);

    std::string report = format_error(*vm.error());
    REQUIRE(report == "runtime error: division by zero (line 2)");
}

TEST_CASE("the report leaves the column out when the VM does not know it", "[vm]") {
    // a chunk carries a line per byte and no column, so there is nothing to point
    // a caret at. the line on its own is still worth printing; a column of 1 that
    // was never measured would just be a lie that happens to look precise.
    std::string source = "let x = 1;\nlet y = x / 0;\n";

    Chunk chunk;
    write_constant(chunk, int64_t{1}, 2);
    write_constant(chunk, int64_t{0}, 2);
    chunk.write(Opcode::Div, 2);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.error() != nullptr);
    REQUIRE(vm.error()->column() == 0);

    // the source overload falls back to the plain report when the position is not
    // precise enough to place a caret on it.
    REQUIRE(format_error(*vm.error(), source) == format_error(*vm.error()));
}

TEST_CASE("nothing is on the stack trace yet", "[vm]") {
    // step 82 is what gives the VM call frames to walk. an empty trace prints as
    // no stack section at all, which is the right answer for a program that only
    // ever ran at the top level.
    Chunk chunk;
    write_constant(chunk, int64_t{1}, 1);
    write_constant(chunk, int64_t{0}, 1);
    chunk.write(Opcode::Mod, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.error() != nullptr);
    REQUIRE(vm.error()->trace().empty());
}

TEST_CASE("DefineGlobal binds the value on top to the name", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{42});
    write_global(chunk, Opcode::DefineGlobal, "x");
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.global("x") != nullptr);
    REQUIRE(std::get<int64_t>(*vm.global("x")) == 42);
}

TEST_CASE("DefineGlobal takes the value off the stack", "[vm]") {
    // `let` is a statement, so it has to leave the stack the way it found it.
    // everything from step 76 on is built on that holding.
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    write_global(chunk, Opcode::DefineGlobal, "x");
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 0);
}

TEST_CASE("GetGlobal pushes what the name was bound to", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{7});
    write_global(chunk, Opcode::DefineGlobal, "n");
    write_global(chunk, Opcode::GetGlobal, "n");
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 7);
}

TEST_CASE("a global read twice pushes it twice", "[vm]") {
    // reading does not consume the binding, which is what lets `n + n` work.
    Chunk chunk;
    write_constant(chunk, int64_t{3});
    write_global(chunk, Opcode::DefineGlobal, "n");
    write_global(chunk, Opcode::GetGlobal, "n");
    write_global(chunk, Opcode::GetGlobal, "n");
    chunk.write(Opcode::Add, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 6);
}

TEST_CASE("defining a name a second time replaces the old binding", "[vm]") {
    // what Environment::define does, so the tree-walker allows it too and the
    // two backends have to agree.
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    write_global(chunk, Opcode::DefineGlobal, "x");
    write_constant(chunk, int64_t{2});
    write_global(chunk, Opcode::DefineGlobal, "x");
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(std::get<int64_t>(*vm.global("x")) == 2);
}

TEST_CASE("reading a name nobody bound is an error", "[vm]") {
    Chunk chunk;
    write_global(chunk, Opcode::GetGlobal, "missing", 3);
    chunk.write(Opcode::Return, 3);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.error() != nullptr);
    REQUIRE(std::string(vm.error()->what()) == "undefined variable 'missing'");
    REQUIRE(vm.error()->line() == 3);
}

TEST_CASE("the undefined variable message matches the tree-walker's", "[vm]") {
    // the two backends are supposed to be indistinguishable from the outside,
    // and the error text is the part a user actually reads. Interpreter's
    // visit_identifier says exactly this.
    Chunk chunk;
    write_global(chunk, Opcode::GetGlobal, "count", 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(format_error(*vm.error()) == "runtime error: undefined variable 'count' (line 1)");
}

TEST_CASE("globals outlive the run that defined them", "[vm]") {
    // the repl keeps one VM and compiles each line into its own chunk, so a
    // `let` typed on one line has to still be there on the next. the stack is
    // cleared between runs; this deliberately is not.
    Chunk first;
    write_constant(first, std::string("hi"));
    write_global(first, Opcode::DefineGlobal, "greeting");
    first.write(Opcode::Return, 1);

    Chunk second;
    write_global(second, Opcode::GetGlobal, "greeting");
    second.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(first) == InterpretResult::Ok);
    REQUIRE(vm.run(second) == InterpretResult::Ok);
    REQUIRE(std::get<std::string>(vm.stack_top()) == "hi");
}

TEST_CASE("a failed run does not lose the globals defined before it", "[vm]") {
    // fail() clears the stack so the next repl line is not handed junk, and it
    // would be easy to clear the globals in the same breath. that would throw
    // away a whole session over one typo.
    Chunk chunk;
    write_constant(chunk, int64_t{5});
    write_global(chunk, Opcode::DefineGlobal, "kept");
    write_global(chunk, Opcode::GetGlobal, "gone");
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.global("kept") != nullptr);
    REQUIRE(std::get<int64_t>(*vm.global("kept")) == 5);
}

TEST_CASE("SetGlobal writes over an existing binding", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    write_global(chunk, Opcode::DefineGlobal, "a");
    write_constant(chunk, int64_t{2});
    write_global(chunk, Opcode::SetGlobal, "a");
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(std::get<int64_t>(*vm.global("a")) == 2);
}

TEST_CASE("SetGlobal leaves the value it assigned on the stack", "[vm]") {
    // assignment is an expression, so `a = b = 7` needs the inner one to hand
    // its value back. the Pop that balances a statement comes from elsewhere.
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    write_global(chunk, Opcode::DefineGlobal, "a");
    write_constant(chunk, int64_t{9});
    write_global(chunk, Opcode::SetGlobal, "a");
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 9);
}

TEST_CASE("assigning to a name nobody bound is an error, not a definition", "[vm]") {
    // otherwise a typo on the left of an `=` quietly makes a second variable and
    // the original never changes. Environment::assign draws the same line.
    Chunk chunk;
    write_constant(chunk, int64_t{1}, 2);
    write_global(chunk, Opcode::SetGlobal, "nope", 2);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(std::string(vm.error()->what()) == "undefined variable 'nope'");
    REQUIRE(vm.global("nope") == nullptr);
}

TEST_CASE("a global operand that is not a string is reported", "[vm]") {
    // the compiler always puts a string there, so reaching this means a chunk
    // built by hand or an operand byte pointing at the wrong constant. binding a
    // global called "42" would be a much harder thing to find later.
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    std::size_t index = chunk.add_constant(int64_t{99});
    chunk.write(Opcode::DefineGlobal, 1);
    chunk.write(static_cast<uint8_t>(index), 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(std::string(vm.error()->what()) == "global name operand is not a string");
}

TEST_CASE("asking for a global nobody defined gives back nothing", "[vm]") {
    VM vm;
    REQUIRE(vm.global("anything") == nullptr);
}

TEST_CASE("Pop takes the top value off", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    write_constant(chunk, int64_t{2});
    chunk.write(Opcode::Pop, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 1);
}

TEST_CASE("Pop removes one value and not everything above it", "[vm]") {
    // three pushes and two Pops has to leave one. a Pop that cleared the stack
    // would look right in every test with a single value on it.
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    write_constant(chunk, int64_t{2});
    write_constant(chunk, int64_t{3});
    chunk.write(Opcode::Pop, 1);
    chunk.write(Opcode::Pop, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 1);
}

TEST_CASE("a Pop with nothing under it does not take the run down", "[vm]") {
    // the compiler only writes a Pop straight after something that pushed, so
    // this is a chunk built by hand. pop() answers an empty stack with nil
    // instead of reading off the end of the vector.
    Chunk chunk;
    chunk.write(Opcode::Pop, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 0);
}

TEST_CASE("Pop leaves the globals alone", "[vm]") {
    // it only ever means "this statement's value is finished with". a binding
    // made before it is nothing to do with the stack.
    Chunk chunk;
    write_constant(chunk, int64_t{7});
    write_global(chunk, Opcode::DefineGlobal, "a");
    write_global(chunk, Opcode::GetGlobal, "a");
    chunk.write(Opcode::Pop, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 0);
    REQUIRE(std::get<int64_t>(*vm.global("a")) == 7);
}

TEST_CASE("GetLocal pushes a copy of the slot it names", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{5});
    write_local(chunk, Opcode::GetLocal, 0);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);

    // two values, not one. the local is still in slot 0 where the next read of
    // it expects to find it — a GetLocal that moved the value out would work
    // once and then hand back nil.
    REQUIRE(vm.stack_size() == 2);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 5);
}

TEST_CASE("GetLocal counts slots from the bottom of the stack", "[vm]") {
    // the compiler numbers locals in declaration order, so slot 0 is the one
    // declared first and therefore the one furthest down. counting from the top
    // instead would read the right value in every test with one local on the
    // stack and the wrong one everywhere else.
    Chunk chunk;
    write_constant(chunk, int64_t{10});
    write_constant(chunk, int64_t{20});
    write_local(chunk, Opcode::GetLocal, 0);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 10);
}

TEST_CASE("the same local can be read more than once", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{3});
    write_local(chunk, Opcode::GetLocal, 0);
    write_local(chunk, Opcode::GetLocal, 0);
    chunk.write(Opcode::Add, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 6);
}

TEST_CASE("SetLocal writes over the slot", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    write_constant(chunk, int64_t{9});
    write_local(chunk, Opcode::SetLocal, 0);
    chunk.write(Opcode::Pop, 1);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 9);
}

TEST_CASE("SetLocal leaves the value it assigned on the stack", "[vm]") {
    // same rule as SetGlobal. assignment is an expression and `a = b = 7` needs
    // the value to still be there for the outer one to use.
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    write_constant(chunk, int64_t{9});
    write_local(chunk, Opcode::SetLocal, 0);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 2);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 9);
}

TEST_CASE("a slot number past the end of the stack is reported", "[vm]") {
    // the compiler only names a slot it counted onto the stack itself, so this
    // is a hand-built chunk. stopping beats indexing off the end of the vector.
    Chunk chunk;
    write_constant(chunk, int64_t{1}, 3);
    write_local(chunk, Opcode::GetLocal, 4, 3);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.error() != nullptr);
    REQUIRE(std::string(vm.error()->what()) == "local slot 4 is out of range");
    REQUIRE(vm.error()->line() == 3);
}

TEST_CASE("SetLocal checks its slot the same way", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{1}, 2);
    write_local(chunk, Opcode::SetLocal, 7, 2);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(std::string(vm.error()->what()) == "local slot 7 is out of range");
}

TEST_CASE("a local and a global with the same name are two different things", "[vm]") {
    // nothing in the VM ties them together — one is a slot and the other is a
    // map entry, and which of the two an instruction touches was decided by the
    // compiler. writing the local has to leave the global where it was.
    Chunk chunk;
    write_constant(chunk, int64_t{1});
    write_global(chunk, Opcode::DefineGlobal, "x");
    write_constant(chunk, int64_t{2});
    write_constant(chunk, int64_t{3});
    write_local(chunk, Opcode::SetLocal, 0);
    chunk.write(Opcode::Return, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(std::get<int64_t>(*vm.global("x")) == 1);
}

TEST_CASE("Nil pushes nil without touching the pool", "[vm]") {
    Chunk chunk;
    chunk.write(Opcode::Nil, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
    REQUIRE(std::holds_alternative<Nil>(vm.stack_top()));
    REQUIRE(chunk.constants.empty());
}

TEST_CASE("True and False push the bools they name", "[vm]") {
    Chunk chunk;
    chunk.write(Opcode::True, 1);
    chunk.write(Opcode::False, 1);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 2);
    REQUIRE(std::get<bool>(vm.stack_top()) == false);
}

TEST_CASE("Jump skips the instructions it covers", "[vm]") {
    // three bytes for the jump, so counting 2 forward from offset 3 lands on the
    // second Const and the first one never runs.
    Chunk chunk;
    write_jump(chunk, Opcode::Jump, 2);
    write_constant(chunk, int64_t{1});
    write_constant(chunk, int64_t{2});

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 2);
}

TEST_CASE("a Jump of zero carries on with the next instruction", "[vm]") {
    Chunk chunk;
    write_jump(chunk, Opcode::Jump, 0);
    write_constant(chunk, int64_t{7});

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 7);
}

TEST_CASE("JumpIfFalse takes the jump on a false condition", "[vm]") {
    Chunk chunk;
    chunk.write(Opcode::False, 1);
    write_jump(chunk, Opcode::JumpIfFalse, 2);
    write_constant(chunk, int64_t{1});
    write_constant(chunk, int64_t{2});

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 2);
}

TEST_CASE("JumpIfFalse falls through on a true condition", "[vm]") {
    Chunk chunk;
    chunk.write(Opcode::True, 1);
    write_jump(chunk, Opcode::JumpIfFalse, 2);
    write_constant(chunk, int64_t{1});
    write_constant(chunk, int64_t{2});

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 2);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 2);
}

TEST_CASE("JumpIfFalse takes the condition off whichever way it went", "[vm]") {
    // an if would grow the stack by one every time it ran otherwise, and a while
    // loop in the next step runs one over and over.
    Chunk taken;
    taken.write(Opcode::False, 1);
    write_jump(taken, Opcode::JumpIfFalse, 0);

    VM vm;
    REQUIRE(vm.run(taken) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 0);

    Chunk skipped;
    skipped.write(Opcode::True, 1);
    write_jump(skipped, Opcode::JumpIfFalse, 0);

    REQUIRE(vm.run(skipped) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 0);
}

TEST_CASE("JumpIfFalse uses the same truthiness as everything else", "[vm]") {
    // only nil and false are falsy, so a 0 condition does not take the jump.
    Chunk chunk;
    write_constant(chunk, int64_t{0});
    write_jump(chunk, Opcode::JumpIfFalse, 3);
    write_constant(chunk, std::string("ran"));

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(std::get<std::string>(vm.stack_top()) == "ran");
}

TEST_CASE("nil is falsy, so the jump is taken", "[vm]") {
    Chunk chunk;
    chunk.write(Opcode::Nil, 1);
    write_jump(chunk, Opcode::JumpIfFalse, 2);
    write_constant(chunk, std::string("skipped"));

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 0);
}

TEST_CASE("both operand bytes count towards the distance", "[vm]") {
    // 0x01 0x00 is 256, which one byte could not have said. the padding is there
    // so the jump has somewhere real to land.
    Chunk chunk;
    write_jump(chunk, Opcode::Jump, 256);
    for (int i = 0; i < 256; ++i) {
        chunk.write(Opcode::True, 1);
    }
    write_constant(chunk, int64_t{9});

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 9);
}

TEST_CASE("a jump landing on the end of the chunk just stops", "[vm]") {
    Chunk chunk;
    write_constant(chunk, int64_t{4});
    write_jump(chunk, Opcode::Jump, 0);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(std::get<int64_t>(vm.stack_top()) == 4);
}

TEST_CASE("a jump past the end of the chunk is reported", "[vm]") {
    // an unpatched jump reads back as 0xffff, which is why the placeholder is
    // that and not zero — this stops instead of running the branch it was
    // supposed to skip.
    Chunk chunk;
    write_jump(chunk, Opcode::Jump, 0xffff, 6);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.error() != nullptr);
    REQUIRE(std::string(vm.error()->what()) == "jump target 65538 is outside the chunk");
    REQUIRE(vm.error()->line() == 6);
}

TEST_CASE("a JumpIfFalse that lands nowhere is caught the same way", "[vm]") {
    Chunk chunk;
    chunk.write(Opcode::False, 1);
    write_jump(chunk, Opcode::JumpIfFalse, 500);

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.stack_size() == 0);
}
