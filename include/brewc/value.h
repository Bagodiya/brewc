#ifndef BREWC_VALUE_H
#define BREWC_VALUE_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace brewc {

// stands in for the `nil` value. a tiny empty struct rather than something like
// a null pointer, so nil ends up being just one more alternative in the variant
// and the type checks below can treat it like every other case.
struct Nil {};

// a `fn` declaration node. only forward declared here so Value can point at one
// without value.h having to drag in the whole ast header. Environment is forward
// declared for the same reason — the function only holds a pointer to one.
class FnDecl;
class Environment;

// a function is just another kind of value: `fn add(...) {...}` binds a callable
// under `add` the same way `let` binds a number. decl points back at the
// declaration the function came from — the AST outlives the run, so the body,
// params and name are still reachable through it when the call finally happens.
// closure is the scope the fn was declared in; a call hangs its own scope off it
// so the body can find the function's own name (recursion) and whatever else was
// in scope when it was written.
//
// closure is a shared_ptr, so the function keeps its birth scope alive as long as
// the function value is around. that's the whole point of a closure — the fn can
// still reach the variables it grew up with even after that block has run to the
// end. decl stays a plain pointer since the parsed program owns every node and
// hangs around for the whole run.
struct Function {
    FnDecl* decl = nullptr;
    std::shared_ptr<Environment> closure;
};

// builtins like `print` are functions too, but their body is C++ code rather than
// a brew AST — there's nothing to walk, we just call straight into native code.
// forward declared here so Value can list it below; the actual definition needs
// Value to exist first (the callback both takes and returns Values), so it comes
// after the variant.
struct NativeFn;

// the runtime representation of a value while a program is actually running.
// everything so far has been tokens and AST nodes; this is the first type that
// holds a real evaluated result. ints are 64-bit, floats are plain doubles, and
// strings own their own storage. the order of the alternatives lines up with the
// is_* helpers below, so leave it alone. builtins ride in behind a shared_ptr:
// that keeps NativeFn an incomplete type here (its callback mentions Value, which
// isn't finished yet) and dodges the size cycle that a by-value member would make.
using Value = std::variant<Nil, int64_t, double, bool, std::string, Function,
                           std::shared_ptr<NativeFn>>;

// now that Value is a complete type we can spell out the builtin. name is only for
// printing and error messages; call is the actual C++ that runs when brew code
// invokes it. arguments arrive already evaluated, and whatever it returns becomes
// the value of the call expression.
struct NativeFn {
    std::string name;
    std::function<Value(const std::vector<Value>&)> call;
};

// shorthand type checks so the interpreter can say is_int(v) instead of spelling
// out std::holds_alternative<...> every time.
bool is_nil(const Value& value);
bool is_int(const Value& value);
bool is_float(const Value& value);
bool is_bool(const Value& value);
bool is_string(const Value& value);
bool is_function(const Value& value);
bool is_native(const Value& value);

// the value's type as a word, mostly for error messages ("expected int, got
// string").
std::string type_name(const Value& value);

// print a value the way the language itself would show it: strings without the
// surrounding quotes, bools as true/false, nil as "nil", and floats with a
// trailing .0 kept so 3.0 doesn't get mistaken for an int.
std::string to_string(const Value& value);

} // namespace brewc

#endif
