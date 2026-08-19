#include "brewc/vm.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include "brewc/value_ops.h"

namespace brewc {

namespace {

// handed back by stack_top and peek when there is nothing to look at. a real
// value would be wrong and a reference to a temporary would dangle, so it is a
// nil that outlives every caller.
const Value& nothing() {
    static const Value empty = Nil{};
    return empty;
}

// the helpers in value_ops ask for a TokenKind, because they were written for the
// tree-walker and that always has the operator token sitting right there. all the
// VM has is the byte it just read, so this turns one back into the other. giving
// those helpers a second way in that took an Opcode would mean two copies of the
// same table, which is the thing step 68 pulled them out to avoid.
TokenKind token_for(Opcode op) {
    switch (op) {
    case Opcode::Add:
        return TokenKind::Plus;
    case Opcode::Sub:
        return TokenKind::Minus;
    case Opcode::Mul:
        return TokenKind::Star;
    case Opcode::Div:
        return TokenKind::Slash;
    case Opcode::Mod:
        return TokenKind::Percent;
    default:
        throw std::runtime_error("opcode is not arithmetic");
    }
}

// work out lhs <op> rhs. the checks run in the same order visit_binary runs them
// and they have to stay that way, since the two backends are supposed to give the
// same answer: strings first so `+` between two of them joins instead of falling
// through to the error, then int with int so 7 / 2 stays 3, then anything else
// numeric widened to double so 7 / 2.0 is 3.5.
//
// the message on the throw names the opcode rather than the operator the user
// wrote, because by now the token is long gone. step 73 is where these get a line
// number and reach the user at all; today the only thing that survives the throw
// is that the run stopped.
Value arithmetic(Opcode op, const Value& lhs, const Value& rhs) {
    TokenKind kind = token_for(op);

    if (op == Opcode::Add && is_string(lhs) && is_string(rhs)) {
        return std::get<std::string>(lhs) + std::get<std::string>(rhs);
    }

    if (is_int(lhs) && is_int(rhs)) {
        return apply_int(kind, std::get<int64_t>(lhs), std::get<int64_t>(rhs));
    }

    if (is_number(lhs) && is_number(rhs)) {
        return apply_float(kind, to_double(lhs), to_double(rhs));
    }

    throw std::runtime_error("cannot apply " + opcode_name(op) + " to " + type_name(lhs) + " and " +
                             type_name(rhs));
}

// flip the sign of one number. an int stays an int so -3 does not come back as
// -3.0, and the detour through the unsigned type is for the one value that has no
// positive twin: writing -v when v is the most negative int64 is undefined
// behaviour rather than the wraparound you would expect. every other value comes
// out the same either way. the tree-walker does the same thing in visit_unary.
Value negate(const Value& operand) {
    if (is_int(operand)) {
        std::uint64_t bits = static_cast<std::uint64_t>(std::get<int64_t>(operand));
        return static_cast<int64_t>(0u - bits);
    }
    if (is_float(operand)) {
        return -std::get<double>(operand);
    }
    throw std::runtime_error("cannot negate " + type_name(operand));
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

        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::Div:
        case Opcode::Mod: {
            // right comes off first because it went on last. the compiler emits
            // the left side before the right, so by the time this instruction
            // runs the right operand is the one on top. popping them the other
            // way round still runs and still leaves one value behind — it just
            // works out b - a, which is the kind of wrong that no crash points
            // at.
            Value rhs = pop();
            Value lhs = pop();

            try {
                push(arithmetic(op, lhs, rhs));
            } catch (const std::runtime_error&) {
                // dividing by zero, or adding an int to a bool. the message is
                // dropped for now since there is nowhere for it to go; step 73
                // gives the VM somewhere to keep it and a line number to go with
                // it.
                return InterpretResult::RuntimeError;
            }
            break;
        }

        case Opcode::Negate: {
            Value operand = pop();

            try {
                push(negate(operand));
            } catch (const std::runtime_error&) {
                return InterpretResult::RuntimeError;
            }
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
