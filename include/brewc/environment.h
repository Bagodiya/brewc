#ifndef BREWC_ENVIRONMENT_H
#define BREWC_ENVIRONMENT_H

#include <string>
#include <unordered_map>

#include "brewc/value.h"

namespace brewc {

// where variables live while a program runs. every block and function body gets
// its own Environment, and a nested scope points at the one it sits inside through
// parent. a lookup that misses locally keeps walking outward until it finds the
// name or runs off the top. parent is non-owning on purpose — the interpreter is
// the thing that keeps the scopes alive; an Environment never frees the one above
// it.
class Environment {
public:
    Environment() = default;
    explicit Environment(Environment* enclosing) : parent_(enclosing) {}

    // bind a name in *this* scope. this is what `let` uses. defining a name that
    // already exists here just overwrites it, which is also how an inner scope
    // shadows an outer one without disturbing it.
    void define(const std::string& name, Value value);

    // find a name starting here and walking outward. hands back a pointer to the
    // stored slot so the caller can read it or change it in place, or nullptr when
    // the name isn't bound anywhere in the chain.
    Value* get(const std::string& name);

    // write to a binding that already exists, hunting for it outward the same way
    // get does. returns false when the name was never defined — assignment isn't
    // allowed to invent new variables, that's define's job.
    bool assign(const std::string& name, Value value);

private:
    std::unordered_map<std::string, Value> values_;
    Environment* parent_ = nullptr;
};

} // namespace brewc

#endif
