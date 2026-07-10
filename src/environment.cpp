#include "brewc/environment.h"

#include <utility>

namespace brewc {

void Environment::define(const std::string& name, Value value) {
    values_[name] = std::move(value);
}

Value* Environment::get(const std::string& name) {
    // walk outward with a plain pointer — following the chain doesn't own
    // anything, the shared_ptrs already keep every scope alive for us.
    for (Environment* scope = this; scope != nullptr; scope = scope->parent_.get()) {
        auto it = scope->values_.find(name);
        if (it != scope->values_.end()) {
            return &it->second;
        }
    }
    return nullptr;
}

bool Environment::assign(const std::string& name, Value value) {
    for (Environment* scope = this; scope != nullptr; scope = scope->parent_.get()) {
        auto it = scope->values_.find(name);
        if (it != scope->values_.end()) {
            it->second = std::move(value);
            return true;
        }
    }
    return false;
}

} // namespace brewc
