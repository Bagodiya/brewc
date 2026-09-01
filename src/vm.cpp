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
//
// only the opcodes that stand for an operator the user can write are in here.
// there is no LessEqual or GreaterEqual case because there are no such opcodes:
// the compiler turns `a <= b` into Greater followed by Not, so what reaches this
// switch is always one of the four below.
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
    case Opcode::Equal:
        return TokenKind::EqualEqual;
    case Opcode::NotEqual:
        return TokenKind::BangEqual;
    case Opcode::Less:
        return TokenKind::Less;
    case Opcode::Greater:
        return TokenKind::Greater;
    default:
        throw std::runtime_error("opcode has no operator");
    }
}

// how the operator behind an opcode was spelled in the source. the tree-walker
// drops expr.op.lexeme straight into its messages and there is no token left here
// to read one off, so the same handful of strings sit in this table instead.
// naming the opcode would have been less work, but then "cannot apply Div" out of
// the VM and "cannot apply '/'" out of the interpreter describe the same mistake
// two different ways, and a user has no idea which backend ran their program.
std::string operator_text(Opcode op) {
    switch (op) {
    case Opcode::Add:
        return "+";
    case Opcode::Sub:
        return "-";
    case Opcode::Mul:
        return "*";
    case Opcode::Div:
        return "/";
    case Opcode::Mod:
        return "%";
    case Opcode::Equal:
        return "==";
    case Opcode::NotEqual:
        return "!=";
    case Opcode::Less:
        return "<";
    case Opcode::Greater:
        return ">";
    default:
        return opcode_name(op);
    }
}

// work out lhs <op> rhs. the checks run in the same order visit_binary runs them
// and they have to stay that way, since the two backends are supposed to give the
// same answer: strings first so `+` between two of them joins instead of falling
// through to the error, then int with int so 7 / 2 stays 3, then anything else
// numeric widened to double so 7 / 2.0 is 3.5.
//
// the throw carries a message and nothing else. it is the dispatch loop that
// knows which byte it was running, so that is where the line gets stamped on,
// which is the same split visit_binary uses in the tree-walker.
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

    throw std::runtime_error("cannot apply '" + operator_text(op) + "' to " + type_name(lhs) +
                             " and " + type_name(rhs));
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

// work out lhs <op> rhs for the four comparison opcodes. compare() already knows
// the rules: two ints are compared as ints so nothing is rounded on the way, a
// mixed pair widens to double so 1 < 1.5 answers instead of erroring, and == and
// != work on any pair while ordering only works on numbers. leaving all of that
// where it is means the VM says the same thing about `"a" < "b"` as the
// tree-walker does, which is the whole reason step 68 happened.
bool comparison(Opcode op, const Value& lhs, const Value& rhs) {
    return compare(token_for(op), lhs, rhs);
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

    // a run that goes fine has to leave error() empty, or the last failure would
    // still be sitting there for the caller to find and report a second time.
    error_.reset();

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

        case Opcode::Nil:
            push(Nil{});
            break;

        case Opcode::True:
            push(true);
            break;

        case Opcode::False:
            // no pool entry and no operand byte. these three values never change,
            // so an opcode each is both smaller and faster than a Const pointing
            // at a slot that holds the same thing every time — which is why
            // visit_literal has emitted them since step 65.
            push(false);
            break;

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
            } catch (const std::runtime_error& e) {
                // dividing by zero, or adding an int to a bool. the helper wrote
                // the message and fail() adds the line it happened on, since the
                // helper has no idea where in the program it was called from.
                return fail(e.what(), chunk);
            }
            break;
        }

        case Opcode::Equal:
        case Opcode::NotEqual:
        case Opcode::Less:
        case Opcode::Greater: {
            // same pop order as the arithmetic opcodes, and it matters more here:
            // `a - b` at least looks wrong when it comes out backwards, but `a < b`
            // answered as `b < a` is still a bool and still plausible.
            Value rhs = pop();
            Value lhs = pop();

            try {
                push(comparison(op, lhs, rhs));
            } catch (const std::runtime_error& e) {
                // ordering two strings, or a bool against a number. equality never
                // gets here — any two values can be compared for that, they are
                // just not equal when their kinds differ.
                return fail(e.what(), chunk);
            }
            break;
        }

        case Opcode::Not: {
            // the same truthiness the tree-walker uses, so only nil and false are
            // falsy and `!0` is false rather than true. this cannot fail, which is
            // why there is nothing to catch: every value is either truthy or it is
            // not.
            //
            // it also runs on its own after a Greater or a Less, since that is how
            // <= and >= are built, and inverting a bool is the same operation as
            // inverting anything else.
            push(!is_truthy(pop()));
            break;
        }

        case Opcode::Negate: {
            Value operand = pop();

            try {
                push(negate(operand));
            } catch (const std::runtime_error& e) {
                return fail(e.what(), chunk);
            }
            break;
        }

        case Opcode::DefineGlobal: {
            std::size_t index = read_byte(chunk);
            const Value& name = chunk.constant_at(index);
            if (!is_string(name)) {
                // the compiler always puts a string there, so this is a chunk
                // that was built by hand or one whose operand byte went astray.
                // saying so beats binding a global called "42".
                return fail("global name operand is not a string", chunk);
            }

            // define and not insert. writing `let x` twice replaces the old
            // binding rather than being an error, which is what Environment's
            // define() does and therefore what the tree-walker already allows.
            globals_[std::get<std::string>(name)] = pop();
            break;
        }

        case Opcode::GetGlobal: {
            std::size_t index = read_byte(chunk);
            const Value& name = chunk.constant_at(index);
            if (!is_string(name)) {
                return fail("global name operand is not a string", chunk);
            }

            auto found = globals_.find(std::get<std::string>(name));
            if (found == globals_.end()) {
                // word for word what Interpreter::visit_identifier says. the two
                // backends have to be indistinguishable from the outside, and an
                // error message is the part of that a user actually reads.
                return fail("undefined variable '" + std::get<std::string>(name) + "'", chunk);
            }
            push(found->second);
            break;
        }

        case Opcode::SetGlobal: {
            std::size_t index = read_byte(chunk);
            const Value& name = chunk.constant_at(index);
            if (!is_string(name)) {
                return fail("global name operand is not a string", chunk);
            }

            auto found = globals_.find(std::get<std::string>(name));
            if (found == globals_.end()) {
                // assigning to a name nobody bound is an error and not a quiet
                // definition, so a typo on the left of an `=` is caught instead
                // of creating a second variable that shadows nothing. same rule
                // as Environment::assign.
                return fail("undefined variable '" + std::get<std::string>(name) + "'", chunk);
            }

            // peek, not pop. assignment is an expression and its value is the
            // value assigned, so it stays for whatever is around it — the Pop
            // that balances the statement comes from visit_expr_stmt.
            found->second = peek(0);
            break;
        }

        case Opcode::GetLocal: {
            std::size_t slot = read_byte(chunk);
            if (slot >= stack_.size()) {
                // the compiler only hands out a slot it counted onto the stack
                // itself, so this is a hand-built chunk or one whose operand byte
                // went astray. saying so beats reading past the vector.
                return fail("local slot " + std::to_string(slot) + " is out of range", chunk);
            }

            // a copy onto the top, not a move out of the slot. the variable is
            // still in scope and every later read wants to find it there — this
            // is the difference between reading a local and consuming it.
            push(stack_[slot]);
            break;
        }

        case Opcode::SetLocal: {
            std::size_t slot = read_byte(chunk);
            if (slot >= stack_.size()) {
                return fail("local slot " + std::to_string(slot) + " is out of range", chunk);
            }

            // peek and not pop, same as SetGlobal. assignment is an expression
            // and its value is the value assigned, so it stays put for whatever
            // is around it and the Pop comes from the enclosing statement.
            //
            // there is no undefined-variable check to make here. a slot number
            // only exists because the compiler saw the declaration, so unlike a
            // global there is no way to assign to a local that was never bound.
            stack_[slot] = peek(0);
            break;
        }

        case Opcode::Pop:
            // whatever the statement in front of this left behind. nothing reads
            // the value on the way out, so it is dropped and not moved anywhere
            // first.
            //
            // popping an empty stack hands back nil instead of reading off the
            // end. that never happens on a chunk the compiler wrote, since it
            // only emits a Pop right after something that pushed, but a chunk
            // built by hand in a test can be unbalanced and taking the process
            // down over it would be worse than doing nothing.
            pop();
            break;

        case Opcode::Jump:
        case Opcode::JumpIfFalse: {
            // read the operand before anything else, whether or not the jump
            // ends up being taken. the two bytes are part of the instruction, so
            // falling through without reading them would leave ip_ pointing at
            // half an offset and the VM would run it as an opcode.
            std::size_t distance = read_short(chunk);

            if (op == Opcode::JumpIfFalse) {
                // the condition comes off either way. the expression in front of
                // this instruction pushed it and nothing further along is going
                // to clear it, so leaving it behind when the branch is taken
                // would grow the stack by one for every if the program runs.
                //
                // same truthiness the tree-walker uses, so `if 0` runs its branch
                // and only nil and false skip it.
                if (is_truthy(pop())) {
                    break;
                }
            }

            std::size_t target = ip_ + distance;
            if (target > chunk.size()) {
                // the compiler patches every jump it writes to a spot inside the
                // chunk, so this is a hand-built chunk or one whose operand went
                // astray — including a jump that was emitted and never patched,
                // which is why the placeholder is 0xffff. landing exactly on the
                // end is allowed and just stops the run.
                return fail("jump target " + std::to_string(target) + " is outside the chunk",
                            chunk);
            }
            ip_ = target;
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
            //
            // this one is a hole in the VM and not a mistake in the program, so
            // it reads as such. no user should ever see it once the phase is
            // finished, but until then a wrong-looking message beats a silent
            // stop with nothing to say.
            return fail(opcode_name(op) + " is not implemented yet", chunk);
        }
    }

    // ran off the end without a Return. the compiler always emits one, so this
    // means either an empty chunk or a hand-built one, and neither is worth
    // calling an error.
    return InterpretResult::Ok;
}

const Value* VM::global(const std::string& name) const {
    auto found = globals_.find(name);
    if (found == globals_.end()) return nullptr;
    return &found->second;
}

const RuntimeError* VM::error() const { return error_ ? &*error_ : nullptr; }

InterpretResult VM::fail(const std::string& message, const Chunk& chunk) {
    // ip_ has already moved past whatever read_byte handed back, so the byte
    // before it is the instruction that went wrong. the one exception is a chunk
    // so short it failed before reading anything, and offset 0 is as good an
    // answer as there is for that.
    std::size_t offset = (ip_ > 0) ? ip_ - 1 : 0;

    // column 0, because the chunk keeps one source line per byte and nothing
    // finer than that. format_error leaves the column out of the report when it
    // is 0 rather than printing a made-up one, so the reader is told the line and
    // no more than the VM actually knows.
    //
    // the trace is empty for the same reason: there is only ever the top level to
    // be in until step 82 gives the VM call frames, and an empty one already
    // prints as no stack section at all.
    error_ = RuntimeError(message, chunk.line_at(offset), 0, {});

    // drop whatever the half-finished expression had pushed. the repl keeps one
    // VM for the whole session, so leaving it there would put the next line's
    // operands on top of junk and quietly give it the wrong operands to work
    // with.
    stack_.clear();
    return InterpretResult::RuntimeError;
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

uint16_t VM::read_short(const Chunk& chunk) {
    // high byte first, matching the order the compiler writes them in. reading
    // them the other way round still gives a number, which is what makes this
    // worth stating: a jump would land somewhere plausible instead of failing.
    uint8_t high = read_byte(chunk);
    uint8_t low = read_byte(chunk);
    return static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low);
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
