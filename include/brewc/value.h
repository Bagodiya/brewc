#ifndef BREWC_VALUE_H
#define BREWC_VALUE_H

#include <cstdint>
#include <string>
#include <variant>

namespace brewc {

// stands in for the `nil` value. a tiny empty struct rather than something like
// a null pointer, so nil ends up being just one more alternative in the variant
// and the type checks below can treat it like every other case.
struct Nil {};

// a `fn` declaration node. only forward declared here so Value can point at one
// without value.h having to drag in the whole ast header.
class FnDecl;

// a function is just another kind of value: `fn add(...) {...}` binds a callable
// under `add` the same way `let` binds a number. all we keep is a pointer back to
// the declaration the function came from — the AST outlives the run, so the body,
// params and name are still reachable through it when the call finally happens.
// nothing owns the FnDecl here; the parsed program does.
struct Function {
    FnDecl* decl = nullptr;
};

// the runtime representation of a value while a program is actually running.
// everything so far has been tokens and AST nodes; this is the first type that
// holds a real evaluated result. ints are 64-bit, floats are plain doubles, and
// strings own their own storage. the order of the alternatives lines up with the
// is_* helpers below, so leave it alone.
using Value = std::variant<Nil, int64_t, double, bool, std::string, Function>;

// shorthand type checks so the interpreter can say is_int(v) instead of spelling
// out std::holds_alternative<...> every time.
bool is_nil(const Value& value);
bool is_int(const Value& value);
bool is_float(const Value& value);
bool is_bool(const Value& value);
bool is_string(const Value& value);
bool is_function(const Value& value);

// the value's type as a word, mostly for error messages ("expected int, got
// string").
std::string type_name(const Value& value);

// print a value the way the language itself would show it: strings without the
// surrounding quotes, bools as true/false, nil as "nil", and floats with a
// trailing .0 kept so 3.0 doesn't get mistaken for an int.
std::string to_string(const Value& value);

} // namespace brewc

#endif
