#ifndef BREWC_ENVIRONMENT_H
#define BREWC_ENVIRONMENT_H

#include <memory>
#include <string>
#include <unordered_map>

#include "brewc/value.h"

namespace brewc {

// where variables live while a program runs. every block and function body gets
// its own Environment, and a nested scope points at the one it sits inside through
// parent. a lookup that misses locally keeps walking outward until it finds the
// name or runs off the top.
//
// parent is a shared_ptr now, not a raw one. that's what makes real closures
// work: a `fn` grabs a handle to the scope it was written in, and holding that
// handle keeps the scope alive even after the block that created it has finished
// running. so a function can still read the variables it closed over long after
// the code around it is gone. the parent chain forms a little tree of scopes and
// each scope lives until the last thing pointing at it lets go.
class Environment {
public:
    Environment() = default;
    explicit Environment(std::shared_ptr<Environment> enclosing)
        : parent_(std::move(enclosing)) {}

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
    std::shared_ptr<Environment> parent_;
};

} // namespace brewc

#endif
