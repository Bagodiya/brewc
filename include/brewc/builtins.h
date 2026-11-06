#ifndef BREWC_BUILTINS_H
#define BREWC_BUILTINS_H

#include <memory>
#include <vector>

#include "brewc/value.h"

namespace brewc {

// print and clock. both the interpreter and the vm bind these into their
// globals so the two backends call the exact same objects (not copies).
const std::vector<std::shared_ptr<NativeFn>>& builtins();

} // namespace brewc

#endif
