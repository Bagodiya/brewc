#include "brewc/vm.h"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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

    // a chunk that stopped mid-call left frames behind, and the slots they were
    // counting from went out with the stack clear above, so carrying on inside
    // one would read locals out of a stack that no longer has them.
    frames_.clear();
    ip_ = 0;

    // a run that goes fine has to leave error() empty, or the last failure would
    // still be sitting there for the caller to find and report a second time.
    error_.reset();

    // which chunk the instructions are being read out of. it starts as the one
    // handed in and moves to the callee's on a Call, which is the only reason
    // this is a pointer rather than the parameter being used directly — the
    // dispatch loop stops being about one chunk the moment calls exist.
    //
    // ip_ stays a member because fail() needs it after the loop has been left
    // behind. the chunk does not, since every failure is reported before the
    // frame it happened in is dropped.
    const Chunk* code = &chunk;

    // an empty chunk has no Return to stop at, so the loop condition has to be
    // the one that ends the run. that also covers a chunk whose last instruction
    // was truncated mid-operand: read_byte walks ip past the end and the next
    // check here stops instead of reading whatever is after the vector.
    while (ip_ < code->size()) {
        Opcode op = static_cast<Opcode>(read_byte(*code));

        switch (op) {
        case Opcode::Const: {
            std::size_t index = read_byte(*code);
            // constant_at answers an index the pool does not have with nil rather
            // than crashing, so a chunk written by hand in a test can be wrong
            // without taking the process down with it.
            push(code->constant_at(index));
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
                return fail(e.what(), *code);
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
                return fail(e.what(), *code);
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
                return fail(e.what(), *code);
            }
            break;
        }

        case Opcode::DefineGlobal: {
            std::size_t index = read_byte(*code);
            const Value& name = code->constant_at(index);
            if (!is_string(name)) {
                // the compiler always puts a string there, so this is a chunk
                // that was built by hand or one whose operand byte went astray.
                // saying so beats binding a global called "42".
                return fail("global name operand is not a string", *code);
            }

            // define and not insert. writing `let x` twice replaces the old
            // binding rather than being an error, which is what Environment's
            // define() does and therefore what the tree-walker already allows.
            globals_[std::get<std::string>(name)] = pop();
            break;
        }

        case Opcode::GetGlobal: {
            std::size_t index = read_byte(*code);
            const Value& name = code->constant_at(index);
            if (!is_string(name)) {
                return fail("global name operand is not a string", *code);
            }

            auto found = globals_.find(std::get<std::string>(name));
            if (found == globals_.end()) {
                // word for word what Interpreter::visit_identifier says. the two
                // backends have to be indistinguishable from the outside, and an
                // error message is the part of that a user actually reads.
                return fail("undefined variable '" + std::get<std::string>(name) + "'", *code);
            }
            push(found->second);
            break;
        }

        case Opcode::SetGlobal: {
            std::size_t index = read_byte(*code);
            const Value& name = code->constant_at(index);
            if (!is_string(name)) {
                return fail("global name operand is not a string", *code);
            }

            auto found = globals_.find(std::get<std::string>(name));
            if (found == globals_.end()) {
                // assigning to a name nobody bound is an error and not a quiet
                // definition, so a typo on the left of an `=` is caught instead
                // of creating a second variable that shadows nothing. same rule
                // as Environment::assign.
                return fail("undefined variable '" + std::get<std::string>(name) + "'", *code);
            }

            // peek, not pop. assignment is an expression and its value is the
            // value assigned, so it stays for whatever is around it — the Pop
            // that balances the statement comes from visit_expr_stmt.
            found->second = peek(0);
            break;
        }

        case Opcode::GetLocal: {
            // the operand counts from the frame's slot 0 and not from the bottom
            // of the stack, which is the whole trick that makes recursion work:
            // the same GetLocal 1 in the same chunk reads a different value in
            // every call, because each one brought its own base with it. at the
            // top level there is no frame and the base is 0, so this is what it
            // always was.
            std::size_t offset = read_byte(*code);
            std::size_t slot = frame_base() + offset;
            if (slot >= stack_.size()) {
                // the compiler only hands out a slot it counted onto the stack
                // itself, so this is a hand-built chunk or one whose operand byte
                // went astray. saying so beats reading past the vector.
                //
                // the number in the message is the one the instruction carries,
                // not the absolute slot it worked out to — the first is what a
                // disassembly of the chunk shows and the second would send anyone
                // reading it looking for a byte nobody wrote.
                return fail("local slot " + std::to_string(offset) + " is out of range", *code);
            }

            // a copy onto the top, not a move out of the slot. the variable is
            // still in scope and every later read wants to find it there — this
            // is the difference between reading a local and consuming it.
            push(stack_[slot]);
            break;
        }

        case Opcode::SetLocal: {
            std::size_t offset = read_byte(*code);
            std::size_t slot = frame_base() + offset;
            if (slot >= stack_.size()) {
                return fail("local slot " + std::to_string(offset) + " is out of range", *code);
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
            std::size_t distance = read_short(*code);

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
            if (target > code->size()) {
                // the compiler patches every jump it writes to a spot inside the
                // chunk, so this is a hand-built chunk or one whose operand went
                // astray — including a jump that was emitted and never patched,
                // which is why the placeholder is 0xffff. landing exactly on the
                // end is allowed and just stops the run.
                return fail("jump target " + std::to_string(target) + " is outside the chunk",
                            *code);
            }
            ip_ = target;
            break;
        }

        case Opcode::Loop: {
            // the only instruction that moves ip_ backwards, which is why it is
            // its own opcode and not a Jump with a negative operand: the operand
            // is two unsigned bytes and there is nowhere in it to put a sign.
            std::size_t distance = read_short(*code);

            if (distance > ip_) {
                // std::size_t does not go below zero, so subtracting too much
                // wraps round to an enormous offset and the loop condition in
                // run() reads it as "past the end" and stops with nothing to
                // say. same reasoning as the forward check above — only a
                // hand-built chunk gets here, since the compiler counts the
                // distance from an offset it already went past.
                return fail("loop distance " + std::to_string(distance) +
                                " reaches back past the start of the chunk",
                            *code);
            }

            // no Pop and nothing touched on the stack. the body balanced itself
            // before this ran, which is the whole reason a loop can run any
            // number of times without the stack drifting.
            ip_ -= distance;
            break;
        }

        case Opcode::Call: {
            // how many arguments were pushed. they are sitting on top of the
            // thing being called, since the compiler emits the callee first and
            // then walks the argument list.
            std::size_t argc = read_byte(*code);

            if (argc >= stack_.size()) {
                // a chunk built by hand can name more arguments than were ever
                // pushed, and peek would answer the ones it cannot reach with nil
                // and then call it — which reports the wrong mistake.
                return fail("call wants " + std::to_string(argc) +
                                " arguments but the stack only holds " +
                                std::to_string(stack_.size()),
                            *code);
            }

            // peek and not pop, and this is the one place where that is more than
            // a convenience: the callee stays where it is and becomes slot 0 of
            // the frame about to be pushed, with the arguments already lined up
            // above it as slots 1..n. that is why compile_function reserves slot
            // 0 under an empty name — nothing is copied or rearranged to set a
            // call up, the stack is already in the right shape.
            const Value& callee = peek(argc);
            if (!is_compiled_fn(callee)) {
                // word for word what Interpreter::visit_call says. a value the VM
                // could call but the tree-walker could not, or the other way
                // round, would be the two backends disagreeing about the
                // language and not just about their messages.
                return fail("can only call functions, not " + type_name(callee), *code);
            }

            std::shared_ptr<CompiledFn> fn = std::get<std::shared_ptr<CompiledFn>>(callee);
            if (static_cast<int>(argc) != fn->arity) {
                // same wording again, down to the plural being wrong for one
                // argument. matching the tree-walker matters more than the
                // grammar does, and fixing it is a change to both backends.
                return fail("function '" + fn->name + "' takes " + std::to_string(fn->arity) +
                                " arguments but got " + std::to_string(argc),
                            *code);
            }

            if (frames_.size() >= max_frames) {
                // a recursion with no base case gets here. the C++ stack is not
                // what runs out — the frames are a vector on the heap and the
                // dispatch loop never calls itself — so nothing else would stop
                // it before the allocator did.
                return fail("stack overflow", *code);
            }

            // the ip written down is the caller's, pointing at whatever follows
            // this instruction, and the line is the one the Call itself was
            // recorded under. ip_ has stepped past both the opcode and its
            // operand by now, so the opcode's own byte is two back.
            frames_.push_back(CallFrame{fn, ip_, stack_.size() - argc - 1, code->line_at(ip_ - 2)});

            // from here the loop is reading someone else's chunk. nothing is
            // saved about this one beyond the frame above, since the callee's
            // Return is the only way back and that is what unwinds it.
            code = &fn->chunk;
            ip_ = 0;
            break;
        }

        case Opcode::Return:
            // step 83 makes this hand a value back to the caller and carry on in
            // the chunk the frame remembers. until then it stops the run wherever
            // it is, so a call reaches its body and the body ends the program —
            // which is enough to see a frame being pushed and its locals being
            // read, and not enough to see one being dropped.
            //
            // at the top level this is what it always was: stopping without
            // popping is what leaves the program's result on the stack for
            // stack_top to report.
            return InterpretResult::Ok;

        default:
            // not an opcode at all — every one in the enum has a case above now.
            // a byte that got cast into an Opcode without being one is the only
            // way to land here, so the number is more use in the message than
            // opcode_name would be, since it has nothing to name.
            //
            // stopping is the point. skipping the byte would carry on reading
            // instructions from whatever offset it happened to leave ip_ at,
            // which is how a chunk starts producing wrong answers instead of a
            // failure.
            return fail("unknown opcode " + std::to_string(static_cast<int>(op)), *code);
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
    // the trace is taken before the frames are dropped below, which is the whole
    // reason it is copied into the error instead of being read off the VM later.
    // an error at the top level has no frames and gives back an empty one, and
    // format_error prints that as no stack section at all.
    error_ = RuntimeError(message, chunk.line_at(offset), 0, call_trace());

    // drop whatever the half-finished expression had pushed, and the frames it
    // was pushed inside. the repl keeps one VM for the whole session, so leaving
    // either behind would put the next line's operands on top of junk and run it
    // inside a call that already gave up.
    stack_.clear();
    frames_.clear();
    return InterpretResult::RuntimeError;
}

std::size_t VM::frame_depth() const { return frames_.size(); }

std::size_t VM::frame_base() const {
    // the top level is not a frame, so its locals are counted from the bottom of
    // the stack. that is the same answer the VM gave before calls existed, which
    // is why none of the old local tests had to change.
    if (frames_.empty()) return 0;
    return frames_.back().slot_base;
}

std::vector<TraceFrame> VM::call_trace() const {
    std::vector<TraceFrame> trace;
    trace.reserve(frames_.size());
    for (const CallFrame& frame : frames_) {
        // outermost first, matching the order Interpreter::visit_call pushes onto
        // call_stack_. format_error walks whatever it is given backwards, so
        // handing it this the other way up would print the trace inside out.
        trace.push_back(TraceFrame{frame.fn->name, frame.call_line});
    }
    return trace;
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
