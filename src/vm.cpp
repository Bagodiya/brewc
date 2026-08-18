#include "brewc/vm.h"

#include <utility>

namespace brewc {

namespace {

// handed back by stack_top and peek when there is nothing to look at. a real
// value would be wrong and a reference to a temporary would dangle, so it is a
// nil that outlives every caller.
const Value& nothing() {
    static const Value empty = Nil{};
    return empty;
}

} // namespace

VM::VM() {
    // the stack grows one value at a time and reallocating in the middle of a
    // run costs more than the memory does. 256 is a guess at "deeper than any
    // expression in the test programs" rather than a limit — pushing past it
    // still works, it just pays for the growth.
    stack_.reserve(256);
}

InterpretResult VM::run(const Chunk& chunk) {
    stack_.clear();
    ip_ = 0;

    // an empty chunk has no Return to stop at, so the loop condition has to be
    // the one that ends the run. that also covers a chunk whose last instruction
    // was truncated mid-operand: read_byte walks ip past the end and the next
    // check here stops instead of reading whatever is after the vector.
    while (ip_ < chunk.size()) {
        Opcode op = static_cast<Opcode>(read_byte(chunk));

        switch (op) {
        case Opcode::Const: {
            std::size_t index = read_byte(chunk);
            // constant_at answers an index the pool does not have with nil rather
            // than crashing, so a chunk written by hand in a test can be wrong
            // without taking the process down with it.
            push(chunk.constant_at(index));
            break;
        }

        case Opcode::Return:
            // step 83 makes this hand a value back to the caller of a function.
            // at the top level there is no caller, so it just stops, and stopping
            // without popping is what leaves the program's result on the stack
            // for stack_top to report.
            return InterpretResult::Ok;

        default:
            // everything else is a real opcode the compiler already emits and
            // this step does not run yet. saying so by stopping is the only
            // honest answer — the alternative is skipping the instruction, which
            // would carry on with a stack that no longer matches what the
            // compiler thinks is on it and produce a wrong number instead of a
            // failure.
            return InterpretResult::RuntimeError;
        }
    }

    // ran off the end without a Return. the compiler always emits one, so this
    // means either an empty chunk or a hand-built one, and neither is worth
    // calling an error.
    return InterpretResult::Ok;
}

const Value& VM::stack_top() const {
    if (stack_.empty()) return nothing();
    return stack_.back();
}

std::size_t VM::stack_size() const { return stack_.size(); }

uint8_t VM::read_byte(const Chunk& chunk) {
    if (ip_ >= chunk.size()) {
        // only reachable on a chunk that ends part way through an instruction.
        // ip_ still moves so the loop in run() sees it has passed the end and
        // gives up rather than asking for the same missing byte forever.
        ++ip_;
        return 0;
    }
    return chunk.code[ip_++];
}

void VM::push(Value value) { stack_.push_back(std::move(value)); }

Value VM::pop() {
    if (stack_.empty()) return Nil{};

    Value top = std::move(stack_.back());
    stack_.pop_back();
    return top;
}

const Value& VM::peek(std::size_t distance) const {
    if (distance >= stack_.size()) return nothing();
    return stack_[stack_.size() - 1 - distance];
}

} // namespace brewc
