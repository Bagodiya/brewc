#ifndef BREWC_VALUE_OPS_H
#define BREWC_VALUE_OPS_H

#include <cstdint>
#include <stdexcept>

#include "brewc/token.h"
#include "brewc/value.h"

namespace brewc {

// the rules for what `+`, `<`, `!` and friends actually mean, kept in one place
// so the tree-walker and the bytecode VM can't end up disagreeing about them.
// they used to live in an anonymous namespace inside interpreter.cpp, which was
// fine while there was only one backend; the VM is about to want the same rules
// and copying them over is how `7 / 2` starts giving two different answers
// depending on which one you ran.
//
// everything here throws a bare std::runtime_error with a message and no source
// position. that's on purpose: these helpers only see two values, not where in
// the program they came from. whoever calls them still has the operator token,
// so the interpreter catches and re-throws through fail() with the line and
// column attached, and the VM will do the same with its own instruction offset.

// the int and float cases share the same shape, so each gets its own little
// helper instead of one big switch that has to keep re-checking the operand
// types. division and modulo by zero on ints would be undefined behaviour, so
// those two are guarded; the float side just lets IEEE produce inf/nan.
int64_t apply_int(TokenKind op, int64_t a, int64_t b);

double apply_float(TokenKind op, double a, double b);

bool is_number(const Value& value);

// widen a number to a double so an int and a float have one type to meet in.
// past 2^53 an int64 has more precision than a double does, so a very large int
// loses its low bits on the way through. that only happens once a float is in
// the expression and there's nothing to do about it short of a bignum, so it's
// the same deal every other language with one float type makes.
double to_double(const Value& value);

bool is_comparison(TokenKind op);

// what counts as "true" when a value lands in an if/while condition. only nil
// and a false bool are falsy; everything else is true, including zero and the
// empty string. keeping the rule this small means there's nothing to memorize.
bool is_truthy(const Value& value);

// figure out a == b for the non-number cases. only two values of the exact same
// kind can be equal, so a bool is never equal to a string and so on. nil only
// ever equals nil.
bool values_equal(const Value& a, const Value& b);

// run a comparison operator and hand back a bool. numbers can use any of the
// six; everything else only gets == and !=, since ordering strings or bools
// isn't something the language promises yet.
bool compare(TokenKind op, const Value& lhs, const Value& rhs);

// all six comparisons for one number type. ints and floats both run through here
// because the operators mean the same thing for either, only the T changes.
// stays in the header because it's a template and both the .cpp below and the
// tests instantiate it themselves.
template <typename T>
bool compare_numbers(TokenKind op, T a, T b) {
    switch (op) {
    case TokenKind::EqualEqual:
        return a == b;
    case TokenKind::BangEqual:
        return a != b;
    case TokenKind::Less:
        return a < b;
    case TokenKind::Greater:
        return a > b;
    case TokenKind::LessEqual:
        return a <= b;
    case TokenKind::GreaterEqual:
        return a >= b;
    default:
        throw std::runtime_error("operator is not a comparison");
    }
}

} // namespace brewc

#endif
